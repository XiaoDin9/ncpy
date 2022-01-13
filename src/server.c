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



#include <string.h>
#include <malloc.h>

#include "../include/inner/server.h"

int execute_server(int port, char* path)
{
    int  fsize = filesize(path);
    if (fsize <= 0) {
        printf("服务端发送的文件为空，或不存在.!!! \n");
        return -1;
    }

    // 如果 fsize == CHUNK_SIZE * 10, 那么 maxchunk 的值为 9;
    // 如果 fsize == CHUNK_SIZE * 10 + 20, 那么 maxchunk 的值为 10;
    // maxchunk 表示取值为 [0,...,maxchunk], 数据总共包含 maxchunk + 1 块
    int maxchunk;
    maxchunk = fsize/CHUNK_SIZE + ((fsize % CHUNK_SIZE == 0) ? -1 : 0);         // 大文件发划分成 maxchunk 块，每块大小为 CHUNK_SIZE

    // for some reason basepath isn't working. quick fix...
    // 保存文件名，即"./xxxx/kkk.file" 通过处理后，文件名为: "kkk.file"
    char *filename  = path;
    int  pathlen    = strlen(path);
    for (int i = pathlen-1; i >= 0; --i)
    {
        if (path[i] == '/' || path[i] == '\\') {
            filename = &path[i+1];
            break;
        }
    }

    int timeout_ms = 10000;
    /*  Create server's the socket. */
    int socket = nn_socket(AF_SP, NN_REP);
    if (socket < 0) {
        fprintf (stderr, "nn_socket: %s\n", nn_strerror(nn_errno()));
        return (-1);
    }

    char addr[1024];
    sprintf(addr, "tcp://*:%d", port);
    /*  Bind to the URL.  This will bind to the address and listen
        synchronously; new clients will be accepted asynchronously
        without further action from the calling program. */
    if( nn_bind(socket, addr) < 0 ) {
        fprintf (stderr, "nn_bind: %s\n", nn_strerror (nn_errno ()));
        nn_close (socket);
        return (-1);
    }

    nn_setsockopt(socket, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout_ms, sizeof(int));
    nn_setsockopt(socket, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout_ms, sizeof(int));

    int  rc;
    char *data = NULL;
    int  idlecnt = 0;
    char cmdbuf[sizeof(int) + COMMAND_ID_SIZE];
    char chunkbuf[CHUNK_SIZE + COMMAND_ID_SIZE];

    /*  Main processing loop. */
    while (idlecnt < 100)
    {
        rc = nn_recv(socket, cmdbuf, COMMAND_ID_SIZE + sizeof(int), 0);         // 从client 侧接收到的 cmd (不同的requst 标识符）
        if (rc <= 0)
        {
            idlecnt += 1;
            continue;
        }
        idlecnt = 0;

        if (rc != 5)
        {
            printf("netwok error: incorrect command length.\n");
            break;
        }

        if (cmdbuf[0] == COMMAND_FINISHED)                              // 发送文件完成
        {
            erase_line();
            printf("%s: send complete\n", path);
            break;
        }
        else if (cmdbuf[0] == COMMAND_FILENAME)                         // 发送数据文件的名字
        {
            int bufsize = strlen(filename) + COMMAND_ID_SIZE + 1;
            char* tmpbuf = (char *)malloc(bufsize);
            tmpbuf[0] = COMMAND_FILENAME;
            strcpy(tmpbuf + COMMAND_ID_SIZE, filename);
            nn_send(socket, tmpbuf, bufsize, 0);
            free(tmpbuf);
            continue;
        }
        else if (cmdbuf[0] == COMMAND_GETMAXCHUNK)                      // 发送数据块的大小
        {
            *cmdbuf = COMMAND_GETMAXCHUNK;
            *((int *)(cmdbuf + 1)) = maxchunk;
            nn_send(socket, cmdbuf, sizeof(int) + 1, 0);
            continue;
        }
        else if (cmdbuf[0] == COMMAND_GETCHUNK)                         // 得到数据块
        {
            // 头的长度
            const int headerlen = sizeof(int) + COMMAND_ID_SIZE;
            int id = *((int *)(cmdbuf+1));

            // 如果 id == fsize / CHUNK_SIZE， 说明读取的是最后一块数据了(<= CHUNK_SIZE)，数据大小为: fsize % CHUNK_SIZE
            // 如果 id < fsize / CHUNK_SIZE, 说明这次读取的数据大小为: CHUNK_SIZE 大小。
            int chunklen = (id == fsize/CHUNK_SIZE) ? fsize % CHUNK_SIZE : CHUNK_SIZE;          // 编号为 id 的数据块大小

            char* sendbuf = (char *)malloc(headerlen + chunklen);
            sendbuf[0] = COMMAND_GETCHUNK;                                   // 响应头部数据1（标识数据类型）
            memcpy(sendbuf + COMMAND_ID_SIZE, &id, sizeof(int));       // 响应的头部数据2（标识读取的是文件中的那块数据）
            // 从 path 文件中读取，编号为 id 的chunk 数据到 data 所指向的内存中去
            readchunk(path, &data, id);
            memcpy(sendbuf+headerlen, data, chunklen);                  // 响应的body 数据（真正的server 发给 client 的数据）

            erase_line();
            printf("sending chunk: %d/%d", id+1, maxchunk+1);
            fflush(stdout);

            nn_send(socket, sendbuf, chunklen + headerlen, 0);
            free(sendbuf);

            continue;
        }

        printf("network error (2)\n");
        break;
    }

    if (idlecnt >= 10)
    {
        printf("idle too long, giving up.\n");
    }

    free(data);

    return 0;
}
