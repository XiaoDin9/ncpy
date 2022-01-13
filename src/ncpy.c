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
#include <pthread.h>
#include "../include/inner/io.h"
#include <zconf.h>

#include "../include/inner/ncpy.h"

#include "../include/inner/io.h"
#include "../include/inner/client.h"
#include "../include/inner/server.h"

typedef struct _Args{
    char *ipAddress;
    char *filename;
    int  port;
}Args;


int display_usage()
{
    printf("ncpy -r REMOTE-ADDRESS-TO-RECEIVE-FROM PORT\n");
    printf("ncpy -s PATH-OF-FILE-TO-SEND\n");
    return 1;
}

/** 负责从 TDA1 server 端接收数据  */
void* TDA2_ClientThreadFun( void *arg )
{
    if( arg == NULL ) {
        printf("Must offer input params. !!!");
        assert(0);
    }

    Args *args = (Args *)arg;
    execute_client(args->ipAddress, PORT);                    // TDA2 的 C 端, 从 args->ipAddress:PORT 处开始接收数据

    // 唤醒消费者线程去消费
    {
        std::unique_lock<std::mutex> tempMutex(g_lock);
        g_haveVideoData = true;
        g_cond.notify_one();
    }

    sleep(5);

    return NULL;
}

/** 负责响应 云服务器的请求数据 */
void* TDA2_ServeceThreadFun( void *arg )
{
    if( arg == NULL ) {
        printf("%s\r\n", "Must offer input params. !!!");
        assert(0);
    }

    Args *args = (Args *)arg;

    {
        std::unique_lock<std::mutex> tempMutex(g_lock);
        g_cond.wait(tempMutex, [&] { return g_haveVideoData && !access("/home/dylan/dy_ws/data/client/xxx.mp4", F_OK); });
        g_haveVideoData = false;
    }

    execute_server(args->port, "/home/dylan/dy_ws/data/client/xxx.mp4");         // TDA2 的 S 端，把文件 args->filename 中的数据从 localhost:port 处发送出去（到云服务器）

    return NULL;
}

int main(int argc, char* argv[])
{
    Args args;
    memset( &args, 0, sizeof(args) );

    if (argc == 3 || argc == 4)
    {
        if( strcmp(argv[1], "receive") == 0 || strcmp(argv[1], "-r") == 0 )
            return execute_client(argv[2], atoi(argv[3]));
        else if( strcmp(argv[1], "send") == 0 || strcmp(argv[1], "-s") == 0 )
            return execute_server(PORT,  argv[2]);
    }
    else if (argc == 5
            && (strcmp(argv[1], "receive") == 0 || strcmp(argv[1], "-r") == 0)
            && (strcmp(argv[3], "send") == 0 || strcmp(argv[3], "-s") == 0))            // ./demo -r ipServerAddr -s localFilename
    {
        pthread_t tda2_c_thread;
        pthread_t ida2_s_thread;

        args.ipAddress = argv[2];

        // 启动 client 的线程
        if( pthread_create( &tda2_c_thread, NULL, TDA2_ClientThreadFun, (void *)&args ) != 0 )
            printf("Create tda2 client thread is error. !!! \n");

        // 启动 server 端线程
        args.filename = argv[4];
        args.port = TDA2PORT;
        if( pthread_create( &tda2_c_thread, NULL, TDA2_ServeceThreadFun, (void *)&args ) != 0 )
            printf("Create tda2 server thread is error. !!! \n");
        
        // 等待所有线程完成
        pthread_exit(NULL);

        return 0;         // 开启 client 端
    }

    return display_usage();
}


// 启动两个线程
// 1. client 的线程，主要是去接收 TDA1 server 端的数据
// 2. sercie 的线程，主要是接收 云服务器端的请求，并把数据推送到 云服务器上去
