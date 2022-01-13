/*
 Copyright (c) 2013 Matthew Howlett

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal 
 in the Software without restriction, including without limitation the rights 
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

#include "../include/inner/client.h"

#include <string.h>
#include <malloc.h>

/**
 * C 发送给 S 端的包（包括信息头部 + body 部分，信息头部( command_id + 数据块的编号 i （如果command_id 不是请求数据，i 就是 0））
 * @param socket client 的描述符
 * @param command 命令
 * @param data 数据块的编号 i
 */
static void send_cmd(int socket, char command, int data)
{
    const int bufsize = sizeof(int) + 1;
    char buf[bufsize];

    buf[0] = command;
    *((int *)(buf+1)) = data;
    nn_send(socket, buf, bufsize, 0);
}

/**
 * 接收字符串，并把字符串的值保存到 *data 所指向的内存中去
 * @param socket client 的描述符
 * @param command C 请求 S 的命令
 * @param data 要返回的数据
 */
static void recv_cmd_string(int socket, char command, char** data)
{
    char* buf;
    int rc = nn_recv(socket, &buf, NN_MSG, 0);
    if (buf[0] != command)
    {
        printf("network error: receiving command type %d", command);
        return;
    }

    int len = strlen(buf+1);
    *data = (char *)malloc(len * sizeof(char) + 1);
    strcpy(*data, buf+1);

    nn_freemsg(buf);
}

/**
 * 接收 Int 类型的值 （这里是 getmaxchunk ）
 * @param socket
 * @param command
 * @param data
 */
static void recv_cmd_int(int socket, char command, int* data)
{
    int bufsize = sizeof(int) + 1;
    char buf[bufsize];

    int rc = nn_recv(socket, buf, bufsize, 0);
    if (buf[0] != command)
    {
        printf("network error: receiving command type %d", command);
        return;
    }
    *data = *((int *)(buf+1));

    return;
}

/**
 * C 从 S 端接送数据块编号为 i 的数据，并把包中body 部分保存到 data 所指向的内存空间中去
 * @param socket client 的描述符
 * @param i 数据块编号
 * @param data 待保存包中 body 数据的内存
 * @return 编号为 i 的数据块实际的接收的数据大小
 */
int recv_cmd_chunk(int socket, int i, char* data)
{
    const int headerlen = COMMAND_ID_SIZE + sizeof(int);            // 接收包的头部数据大小（command_id + 数据块的的编号)
    const int buflen = CHUNK_SIZE + headerlen;                      // 接收到的包总共大小 (command_id + 数据块的的编号 + body 大小）
    char buf[buflen];

    // 从 S 端接收的数据放入 buflen 缓冲区中
    int rc = nn_recv(socket, buf, buflen, 0);
    if (rc < 0)
    {
        return -1;
    }

    // 校验包头的 command_id 是否为  COMMAND_GETCHUNK 来校验
    if (buf[0] != COMMAND_GETCHUNK)
    {
        return -2;
    }

    // 校验包头的 数据块 id
    int goti = *((int *)(buf+1));
    if (goti != i)
    {
        return -3;
    }

    int chunklen = rc - headerlen;                      // 包中包含实际数据的大小 chunklen
    memcpy(data, buf + headerlen, chunklen);        // 并把 buf 缓冲区中包含数据的那部分 copy 给 data所指向的内存空间中去

    return chunklen;
}

/**
 * 得到从 server 端接收的文件名 和 数据块的总数（从0开始的）
 * @param socket 客户端的描述符
 * @param filename 文件名
 * @param maxchunk 总块数
 * @return
 */
static void get_filename_and_numchunks(int socket, char** filename, int* maxchunk)
{
    // this part is not robust against network outages.
    // 这部分对网络中断不可靠。

    // C 端给 S 端发送  COMMAND_FILENAME 请求
    send_cmd(socket, COMMAND_FILENAME, 0);
    // S 端响应 C 端（COMMAND_FILENAME）的数据，保存到 filename 中去
    recv_cmd_string(socket, COMMAND_FILENAME, filename);

    send_cmd(socket, COMMAND_GETMAXCHUNK, 0);
    recv_cmd_int(socket, COMMAND_GETMAXCHUNK, maxchunk);
}

int execute_client(char* a, uint32_t port)
{
    int  rc;

    char addr[1024];
    sprintf(addr, "tcp://%s:%d", a, port);

    int socket = nn_socket(AF_SP, NN_REQ);
    if (socket < 0) {
        fprintf (stderr, "nn_socket: %s\n", nn_strerror(nn_errno()));
        return (-1);
    }

    if( nn_connect(socket, addr) < 0 ) {
        fprintf (stderr, "nn_socket: %s\n", nn_strerror(nn_errno()));
        nn_close (socket);
        return (-1);
    }

    char *filename;
    int  maxchunk;
    get_filename_and_numchunks(socket, &filename, &maxchunk);

    int bufsize = CHUNK_SIZE * (maxchunk + 1);
    char *data = (char *)malloc(bufsize);           // 接收数据的大小
    assert( data != NULL );

    int timeout_ms = 10000;
    nn_setsockopt(socket, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout_ms, sizeof(int));
    nn_setsockopt(socket, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout_ms, sizeof(int));

    // 从 S 端接收数据，总共需要接收 maxchunk + 1 块数据
    char chunkbuf[CHUNK_SIZE];
    for( int i=0; i <= maxchunk; ++i )
    {
        erase_line();
        printf( "receiving chunk %d/%d", i+1, maxchunk+1 );
        fflush(stdout);

        // C 端向 S 端发送 COMMAND_GETCHUNK 请求（请求真正的数据）
        send_cmd(socket, COMMAND_GETCHUNK, i);

        rc = -1;
        int errorcount = 0;
        // 如果接收编号为 i 的数据块，中途出现错误，反复的接收编号为 i 的数据块（应对网络不佳、C 从 S 端接收数据）
        // 可以通过 errorcount 来决定最大容忍的一个接收次数
        while (rc < 0)
        {
            rc = recv_cmd_chunk(socket, i, chunkbuf);           // 接收编号为 i 的数据块，并把body 部分保存到 chunkbuf 缓冲区中
            if (rc < 0)
            {
                errorcount += 1;
                erase_line();
                printf("receiving chunk %d/%d [recv error x%d]", i+1, maxchunk+1, errorcount);
                fflush(stdout);

                nn_close(socket);
                nn_socket(AF_SP, NN_REQ);
                nn_connect(socket, addr);
                nn_setsockopt(socket, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout_ms, sizeof(int));
                nn_setsockopt(socket, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout_ms, sizeof(int));
                send_cmd(socket, COMMAND_GETCHUNK, i);
            }
        }

        // 接收编号为 i 的数据成功，并把接收的数据(body) 保存到文件 filename 中去（filename 跟 S 端的一样）
        if (rc > 0)
        {
            appendtofile(filename, chunkbuf, rc);
        }
    }

    // C 端发送 S 端一个数据接收完成的命令 COMMAND_FINISHED
    send_cmd(socket, COMMAND_FINISHED, 0);

#if 1
    // 把数据 mv 到其他目录中去 <-> 优化：直接修改文件的名字
    rename(filename, "./xxx.mp4");
#endif

    erase_line();
    printf ("%s: receive complete\n", filename);

    free(filename);

    return rc;
}
