#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>

#define MAX_EVENTS     512

#define BUFF_MAX_LEN   1024*1024

int g_sockfd = -1;

struct ss_buff {
    char   body[BUFF_MAX_LEN];
    int    write;
    int    read;
    struct ss_buff * pnext;
};

struct ss_buff_m {
    struct ss_buff * pnext;
    struct ss_buff * ptail;
};

struct ss_buff_m g_buff_m = {NULL, NULL};


int ss_buff_read(struct ss_buff * pbuff, char *buf, size_t nbytes)
{
    int copy_size;
    
    if ( ( pbuff->read + nbytes) > pbuff->write )
    {
        copy_size = pbuff->write - pbuff->read;
    }
    else
    {
        copy_size = nbytes;
    }

    if ( 0 == copy_size )
    {
        return 0;
    }

    memcpy(buf, &pbuff->body[pbuff->read], copy_size );
    pbuff->read += copy_size;

    return copy_size;
}

int ss_buff_write(struct ss_buff * pbuff, const char *buf, size_t nbytes)
{
    int copy_size;

    if ( ( pbuff->write + nbytes ) > BUFF_MAX_LEN )
    {
        copy_size = BUFF_MAX_LEN - pbuff->write;
    }
    else
    {
        copy_size = nbytes;
    }

    if ( 0 == copy_size )
    {
        return 0;
    }

    memcpy( &pbuff->body[pbuff->write], buf, copy_size );
    pbuff->write += copy_size;

    return copy_size;
}

int ss_buff_m_read(struct ss_buff_m * pbuff, char *buf, size_t nbytes)
{
    size_t cnt = 0;
    size_t remain = nbytes;
    struct ss_buff * pcur = pbuff->pnext;

    while( pcur != NULL )
    {
        size_t tmp = ss_buff_read(pcur, buf + cnt, remain);

        if ( 0 == tmp )
        {
            pbuff->pnext = pcur->pnext;
            free(pcur);
            pcur = pbuff->pnext;

            continue;
        }

        cnt    += tmp;
        remain -= tmp;

        if ( 0 == remain )
        {
            break;
        }
    }

    if ( NULL == pbuff->pnext )
    {
        pbuff->ptail = NULL;
    }

    return cnt;
}


int ss_buff_m_write(struct ss_buff_m * pbuff, const char *buf, size_t nbytes)
{
    size_t cnt = 0;
    size_t remain = nbytes;
    struct ss_buff * pcur = pbuff->ptail;

    for ( ; remain != 0 ; )
    {
        if ( NULL == pcur )
        {
alloc:
            pcur = (struct ss_buff *)malloc(sizeof(struct ss_buff));
            pcur->read  = 0;
            pcur->write = 0;
            pcur->pnext = NULL;

            if ( NULL == pbuff->ptail )
            {
                pbuff->pnext = pcur;
                pbuff->ptail = pcur;
            }
            else
            {
                pbuff->ptail->pnext = pcur;
                pbuff->ptail = pcur;
            }
        }

        size_t tmp = ss_buff_write(pcur, buf + cnt, remain );
        if ( 0 == tmp )
        {
            goto alloc;
        }

        cnt    += tmp;
        remain -= tmp;
    }

    return cnt;
}

unsigned long gettime()
{
    struct timespec time_now;
    clock_gettime(CLOCK_MONOTONIC, &time_now);
    return ((unsigned long)time_now.tv_sec * 1000000) + (unsigned long)(time_now.tv_nsec / 1000);
}

unsigned long parsetime(const char * buff, const char * prefix )
{
    unsigned long timestamp = 0;
    char * ptemp;
    
    ptemp = strstr(buff, prefix);
    if ( NULL == ptemp )
    {
        return 0;
    }

    if ( sscanf(ptemp + strlen(prefix), "%lu", &timestamp ) != 1 )
    {
        return 0;
    }

    return timestamp;
}

unsigned long stat_delay_times = 0;

int stat_send_times    = 0;
size_t stat_send_size  = 0;

int stat_recv_times    = 0;
size_t stat_recv_size  = 0;

void * stat_display(void * arg) 
{
    int send_times = 0;
    int recv_times = 0;

    size_t send_size = 0;
    size_t recv_size = 0;

    send_times = stat_send_times;
    recv_times = stat_recv_times;
    send_size  = stat_send_size;
    recv_size  = stat_recv_size;
    
    for (;;)
    {
        sleep(5);

        if (( stat_send_times - send_times) != 0 )
        {
            printf(" stat send times %d \n", (stat_send_times - send_times)/5 );
            printf(" stat send size  %lu \n", (stat_send_size  - send_size )/5 );
        }

        if (( stat_recv_times - recv_times ) != 0 )
        {
            printf(" stat recv times %d \n", (stat_recv_times - recv_times)/5 );
            printf(" stat recv size  %lu \n", (stat_recv_size  - recv_size )/5 );
        }

        send_times = stat_send_times;
        recv_times = stat_recv_times;
        send_size  = stat_send_size;
        recv_size  = stat_recv_size;
    }
}

void * server_socket_process(void * arg)
{
    int ret;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    int epfd;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if ( epfd < 0 )
    {
        printf("epoll create failed\n");
        exit(1);
    }

    ev.data.fd = g_sockfd;
    ev.events  = EPOLLIN;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, g_sockfd, &ev);
    if (ret < 0) 
    {
        printf("listen failed\n");
        exit(1);
    }

    for (;;)
    {
        /* Wait for events to happen */
        int nevents = epoll_wait(epfd,  events, MAX_EVENTS, -1);
        int i;

        for ( i = 0; i < nevents ; ++i ) 
        {
            /* Handle new connect */
            if (events[i].data.fd == g_sockfd) 
            {
                while (1) 
                {
                    int nclientfd = accept(g_sockfd, NULL, NULL);
                    if (nclientfd < 0) {
                        break;
                    }
        
                    printf("accept client fd %d.\n", nclientfd);
        
                    /* Add to event list */
                    ev.data.fd = nclientfd;
                    ev.events  = EPOLLIN | EPOLLOUT | EPOLLERR;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, nclientfd, &ev) != 0) 
                    {
                        printf("epoll_ctl failed:%d, %s\n", errno, strerror(errno));
                        break;
                    }
                }
            } 
            else
            { 
                char buf[BUFF_MAX_LEN];
                ssize_t writelen = 0;
                ssize_t readlen  = 0;

                if (events[i].events & EPOLLERR ) 
                {
                    /* Simply close socket */
                    epoll_ctl(epfd, EPOLL_CTL_DEL,  events[i].data.fd, NULL);
                    close(events[i].data.fd);
                    printf("connect %d close.\n", events[i].data.fd);
                } 

                if (events[i].events & EPOLLIN )
                {
                    readlen = read( events[i].data.fd, buf, sizeof(buf));
                    if ( readlen > 0 )
                    {
                        stat_recv_times++;
                        stat_recv_size += readlen;
                        ss_buff_m_write(&g_buff_m, buf, readlen);
                    }
                }

                if (events[i].events & EPOLLOUT )
                {
                    writelen = ss_buff_m_read(&g_buff_m, buf, sizeof(buf));
                    if ( writelen > 0 )
                    {
                        write( events[i].data.fd, buf, writelen);
                        stat_send_times++;
                        stat_send_size += writelen;
                    }
                }
            }
        }
    }
}

void * client_socket_process(void * arg)
{
    int ret;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    int epfd;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if ( epfd < 0 )
    {
        printf("epoll create failed\n");
        exit(1);
    }

    ev.data.fd = g_sockfd;
    ev.events  = EPOLLIN | EPOLLOUT | EPOLLERR;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, g_sockfd, &ev);
    if (ret < 0) 
    {
        printf("listen failed\n");
        exit(1);
    }

    for (;;)
    {
        /* Wait for events to happen */
        int nevents = epoll_wait(epfd,  events, MAX_EVENTS, -1);
        int i;

        for ( i = 0; i < nevents ; ++i ) 
        {
            char buf[BUFF_MAX_LEN];
            size_t writelen = 0;
            size_t readlen  = 0;

            if (events[i].events & EPOLLERR ) 
            {
                /* Simply close socket */
                epoll_ctl(epfd, EPOLL_CTL_DEL,  events[i].data.fd, NULL);
                close(events[i].data.fd);
                printf("connect %d close.\n", events[i].data.fd);
            } 

            if (events[i].events & EPOLLIN )
            {
                readlen = read( events[i].data.fd, buf, sizeof(buf));
                if ( readlen > 0 )
                {
                    unsigned long timeold;
                    unsigned long timenow = gettime();

                    timeold = parsetime(buf, "[timestamp1");
                    if ( timeold > 0 )
                    {
                        stat_delay_times += (timenow - timeold);
                    }
                    
                    stat_recv_times++;
                    stat_recv_size += readlen;
                }
            }

            if (events[i].events & EPOLLOUT )
            {
                if ( stat_send_times == stat_recv_times )
                {
                    if ( stat_send_times > 10000 )
                    {
                        unsigned long timenow = gettime();
                        
                        printf("[%lu] avg network delay : %lu us\n", timenow, stat_delay_times / stat_send_times );
                        
                        stat_send_times = 0;
                        stat_send_size  = 0;
                        stat_recv_times = 0;
                        stat_recv_size  = 0;

                        stat_delay_times = 0;
                    }
                    else
                    {
                        readlen = sprintf(buf, "hello world! [timestamp1 %lu]", gettime());
                        writelen = write( events[i].data.fd, buf, readlen);
                        if ( writelen > 0 )
                        {
                            stat_send_times++;
                            stat_send_size += writelen;
                        }
                    }
                }
            }
        }
    }
}


int server_init(char * addr, short port)
{
    int ret;
    int i;
    struct epoll_event ev;
    
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0)
    {
        printf("socket failed\n");
        exit(1);
    }

    printf("sockfd:%d\n", g_sockfd);
    printf("listen -> %s:%d\n", addr, port);

    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    
    ret = inet_aton(addr, &my_addr.sin_addr);
    if (ret < 0)
    {
        printf("inet_aton failed! \n");
        exit(1);
    }

    ret = bind(g_sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if (ret < 0) 
    {
        printf("bind failed\n");
        exit(1);
    }

    ret = listen(g_sockfd, MAX_EVENTS);
    if (ret < 0) 
    {
        printf("listen failed\n");
        exit(1);
    }

    int on = 1;
    ret = ioctl(g_sockfd, FIONBIO, &on);
    if (ret < 0) 
    {
        printf("ioctl failed\n");
        exit(1);
    }

    server_socket_process(NULL);

    return 0;
}


int client_init(char * addr, short port)
{
    int ret;
    int i;

    g_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sockfd < 0)
    {
        printf("socket failed\n");
        exit(1);
    }
        
    printf("sockfd:%d\n", g_sockfd);
    
    printf("connect -> %s:%d\n", addr, port);

    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));
    
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);

    ret = inet_aton(addr, &my_addr.sin_addr);
    if (ret < 0)
    {
        printf("inet_aton failed! \n");
        exit(1);
    }

    ret = connect(g_sockfd, (const struct sockaddr *)&my_addr, sizeof(my_addr));
    if (ret < 0) 
    {
        printf("connect failed! errno %d\n", errno);
        exit(1);
    }

    client_socket_process(NULL);

    return 0;
}



int main(int argc, char * argv[])
{
    int i;
    int flag = 0;
    short port;
    char * addr;
    pthread_t tid;

    for ( i = 0 ; i < argc ; i++ )
    {
        if ( 0 == strcmp(argv[i],"server") )
        {
            flag = 1;
        }
        else if ( 0 == strcmp(argv[i],"client") )
        {
            flag = 0;
        }

        if ( 0 == strcmp(argv[i],"port") )
        {
            port = (short)atoi(argv[i+1]);
        }

        if ( 0 == strcmp(argv[i],"add") )
        {
            addr = argv[i+1];
        }
    }

    if ( flag )
    {
        pthread_create(&tid, NULL, stat_display, NULL);        
        server_init(addr, port);
    }
    else
    {
        client_init(addr, port);
    }
}

