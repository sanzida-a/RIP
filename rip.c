#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netdb.h> 
#include <unistd.h>
#include <pthread.h>
#define UPDATE 5
#define TIMEOUT UPDATE*6
#define GARBAGE UPDATE*4
 
struct Neighbour                       
{
    int port;
    int metric;
    int id;
};

struct Interface                  
{
    int port;
    int sockfd;             
    struct Neighbour *dest;
    bool got_ne;
    pthread_t listener;             
    pthread_mutex_t send_socket;   
};

struct Fileinfo                 
{
    int id;
    int inum;
    int onum;
    bool id_status;
    struct Interface *input;
    struct Neighbour *output;
}self;
 

struct AllTimer                 
{
    struct timeval timer;
    void (*fun_ptr)();
    void *args;
    pthread_t timer_thread;    
    bool valid;     
    pthread_mutex_t change_time;
};   

struct Route_Table                  
{   
    struct Route_Table *next;    
    int address;                 
    uint32_t metric;
    int next_hop;
    //bool flag;                 
    struct AllTimer timeout;
    struct AllTimer garbage;
    int interfc; 

}*table ;  

typedef char byte;
struct Header {         
  byte command; 		 
  byte version;             
  short int id;       
};

struct Body {         
  short int addrfamily;
  short int zero;
  uint32_t destination;    
  uint32_t zero1;
  uint32_t zero2;
  uint32_t metric;
};

struct packet              
{
    char *message;
    int size;
};

char *filevar[] =  {"id", "inputs", "outputs"};
bool showlog = false;
bool kill = false;   
pthread_mutex_t access_route_table;
pthread_t updater;

void exit_program()                       
{
    struct Route_Table *node = table ;
    struct Route_Table *prior = node;

    if (pthread_join(updater, NULL) != 0) {
            perror("thread join error");
    }
    for(int i=0; i<self.inum; i++)
    {
        if (pthread_join(self.input[i].listener, NULL) != 0) {
            perror("thread join error");
        }
    }
    printf("Deleting table\n");
    while(node->next != NULL)
    {
        if (pthread_join(node->timeout.timer_thread, NULL) || pthread_join(node->garbage.timer_thread, NULL) != 0) {
            perror("thread join error");
        }
        prior = node;
        node = node->next;
        free(prior);
    }
    free(node);
    free(self.input);
    free(self.output);
    exit(0);
}

int readfile(char *file, struct Fileinfo *item)
{
    char *mark;
    char *line = NULL; 
    size_t len = 0;
    char *name = NULL;
    char *content = NULL;
    char *content_collect = NULL;
    int n = 1;

    FILE *fp = fopen(file, "r");
    
    if (fp == NULL) {

        exit(1);
    } 
    while ((len = getline(&line, &len, fp)) != -1) 
    {
        if(*line != '#') 
        {
            if (line[len - 1] == '\n') 
            {
                line[len - 1] = '\0';
            }

            mark = strchr(line, ':');
            if (mark != NULL)
            {
                //init
                name = (char*)malloc(sizeof(char) * (mark - line + 1));
                content = (char*)malloc(sizeof(char) * (len - (mark - line) + 1));
                content_collect = content;
                memset(name, '\0', mark - line + 1);
                memset(content, '\0', (len - (mark - line) + 1)); 
                strncpy(name, line, mark - line);
                strcpy(content, mark + 1);


                if (strcmp(name,filevar[0]) == 0)            
                {
                    if (item->id_status)
                    {
                        printf("WARNING: Duplicate router-id found at line %d!\n", n);
                    }  
                  
                    else
                    {
                        item->id = atoi(content); 
                        item->id_status = true;                     
                    }
                    
                }
                else if (strcmp(name, filevar[1]) == 0)         //input
                {
                    char *ptr;
                    while ((ptr = strtok(content, ",")) != NULL)
                    {
                            item->inum++;
                            item->input = (struct Interface*)realloc(item->input, sizeof(struct Interface) * item->inum);
                            
                            item->input[item->inum - 1].port = atoi(ptr); 
                            item->input[item->inum - 1].dest = NULL;
                            item->input[item->inum - 1].got_ne = false;
                            item->input[item->inum - 1].sockfd = 0;
                            pthread_mutex_init(&item->input[item->inum - 1].send_socket, NULL);
                            content = NULL;
                    }

                }
                else if (strcmp(name, filevar[2]) == 0)         //output
                {
                    char *ptr;
                    while ((ptr = strtok(content, ",")) != NULL)
                    {
                        char *dash;
                        char temp[8];
                        
                        item->onum++;
                        item->output = (struct Neighbour*)realloc(item->output, sizeof(struct Neighbour) * item->onum);
                        
                        memset(temp, '\0', sizeof(temp));            //output port
                        dash = strchr(ptr, '-');
                        if (dash == NULL)
                        {
                            exit(1);
                        }
                        strncpy(temp, ptr, dash - ptr);
                        item->output[item->onum - 1].port = atoi(temp);
                        
                        item->output[item->onum - 1].metric = 1;

                        memset(temp, '\0', sizeof(temp));           //output id
                        strcpy(temp, dash + 1);
                        item->output[item->onum - 1].id = atoi(temp);
                        content = NULL;
                    }
                }
                free(name);
                free(content_collect);
            }
        }
        n++;   
    }

    free(line);
    fclose(fp);  

    if (self.inum == 0) 
    {
        exit_program();
    }
    if (self.onum == 0) 
    {
        exit_program();
    }

}

void* time_handler(void *args)
{
    struct AllTimer *timerdata = (struct AllTimer *)args;  
    while (timerdata->timer.tv_sec > 0 && timerdata->valid && !kill)  
    {
        
        pthread_mutex_lock(&timerdata->change_time);    
        timerdata->timer.tv_sec --;   
        pthread_mutex_unlock(&timerdata->change_time); 
        sleep(1);        
    }
    if (timerdata->valid && !kill) 
    {
        (timerdata->fun_ptr)(timerdata->args); 
    }
    pthread_exit(0);                  
  
}

void set_time(struct AllTimer *timerdata, pthread_t *timer_thread)
{
    if (pthread_create(timer_thread,NULL,time_handler,timerdata) != 0)
    {
            perror("TimeHandler");
            exit_program();
    }

}

void packet_header(struct packet *msg, struct Header rh)
{   
    msg->size += sizeof(rh);
    msg->message = (char*)malloc(msg->size);
    memset(msg->message, '\0', msg->size);
    memcpy(msg->message, (void*)&rh, sizeof(rh));
}

void packet_entry(struct packet *msg, struct Body re)
{   
    msg->message = (char*)realloc(msg->message, msg->size + sizeof(re));
    memset(msg->message + msg->size, '\0', sizeof(re));
    memcpy(msg->message + msg->size, (void*)&re, sizeof(re));
    msg->size += sizeof(re);
}

//triggering update

void generate_update(struct packet *msg, int nexthop, struct Route_Table *node) 
{ 
    struct Header rh;
    struct Body re;
    rh.command = 1;
    rh.version = 1;
    rh.id = self.id;
    packet_header(msg, rh);

    re.addrfamily = 2;
    re.zero = 0;
    re.zero1 = 0;
    re.zero2 = 0;

    if (node == NULL)  
    {
        struct Route_Table *item = table ;

        
        while(item != NULL)
        {
            if (item->next_hop != nexthop) re.metric = item->metric;
            else re.metric = 16;    
            re.destination = item->address;
            packet_entry(msg, re);

            item = item->next;
        }
    }
    else  
    { 
        if (node->next_hop != nexthop) re.metric = node->metric;
        else re.metric = 16;   
        re.destination = node->address; 

        packet_entry(msg, re);
    }

} 

void send_update(struct Interface *interface, struct Route_Table *node)
{
    int rc;
    struct sockaddr_in remote; 

    remote.sin_family = AF_INET;         
    remote.sin_addr.s_addr = INADDR_ANY; 
    struct packet msg;
    msg.size = 0;

    if (interface->got_ne)    
    {
        remote.sin_port = htons(interface->dest->port);  
        generate_update(&msg, interface->dest->id, node) ;
        pthread_mutex_lock(&interface->send_socket);
        rc = sendto(interface->sockfd, msg.message, msg.size, 0, (struct sockaddr *)&remote, sizeof(remote));
        pthread_mutex_unlock(&interface->send_socket);      
        if (rc == -1) {
            perror("error");
        }
    }
    else  
    {
        generate_update(&msg, -1, node) ; 
        for(int i = 0; i < self.onum; i++)
        {
            remote.sin_port = htons(self.output[i].port);   
            pthread_mutex_lock(&interface->send_socket);
            rc = sendto(interface->sockfd, msg.message, msg.size, 0, (struct sockaddr *)&remote, sizeof(remote));
            pthread_mutex_unlock(&interface->send_socket);
            
            if (rc == -1) {
                perror("error");
            }
        }
        
    }
    free(msg.message);
}


void triggered_update(struct Route_Table *node)
{
    for (int i = 0; i < self.inum; i++)
        {
            send_update(&self.input[i], node);
        }
}

//routing table

void remove_route_table(struct Route_Table *node)  
{

    struct Route_Table *item, *prior;    //table gothrough

    item = table ;
    prior = item;    
    do
    {
        if(item == node)
        {

            prior->next = node->next;
            free(node);
            break;
        }
        prior = item;
        item = item->next;
    }while (item != NULL);

    printf("Table entry deleted\n");
}

void garbage_collect(void* args)       
{
    struct Route_Table *node = (struct Route_Table*) args;
    pthread_mutex_lock(&access_route_table);   
    remove_route_table(node);
    pthread_mutex_unlock(&access_route_table);   

}

void route_table_timeout(void* args)  
{
    struct Route_Table *node = (struct Route_Table*) args;

    pthread_mutex_lock(&access_route_table);        
    node->metric = 16;
    triggered_update(node);         
    pthread_mutex_unlock(&access_route_table);     
    pthread_mutex_lock(&node->garbage.change_time);  
    node->garbage.valid = true;  
    pthread_mutex_unlock(&node->garbage.change_time);  
  
    set_time(&node->garbage, &node->garbage.timer_thread);
    
    if (pthread_join(node->garbage.timer_thread, NULL) != 0)  
            {
                perror("pthread_join");
                exit_program();
            }

    if (!node->garbage.valid)      
    {
        pthread_mutex_lock(&node->timeout.change_time);              
        pthread_mutex_unlock(&node->timeout.change_time);
        pthread_mutex_lock(&node->garbage.change_time);           
        pthread_mutex_unlock(&node->garbage.change_time);
        node->timeout.timer.tv_sec = TIMEOUT;
        node->garbage.timer.tv_sec = GARBAGE;
        set_time(&node->timeout, &node->timeout.timer_thread);   
    }

}

void add_route_table(struct Body *re, int nexthop, int interfc, int cost)  
{
    struct Route_Table *item, *prior;
    item = table ;
    prior = item;
    bool found = false;           
    pthread_mutex_lock(&access_route_table);   
    do                   
    {
        if(item->address == re->destination)
        {
            found = true;                
            break;

        }
        prior = item;
        item = item->next;
        
    }while (item != NULL);
    
    if(found) 
    {
        if(item->metric == re->metric + cost && item->metric != 16 && item->next_hop == nexthop) 
        {
            pthread_mutex_lock(&item->timeout.change_time); 
            item->timeout.timer.tv_sec = TIMEOUT;  
            pthread_mutex_unlock(&item->timeout.change_time);

            if (item->garbage.valid)    
            {
                pthread_mutex_lock(&item->garbage.change_time); 
                item->garbage.valid = false;    
                pthread_mutex_unlock(&item->garbage.change_time); 
            }
            
        }
        else
        {
            if ((item->next_hop == nexthop) || (item->next_hop != nexthop && item->metric > re->metric + cost))
            {
                if (re->metric + cost >= 16)    
                {                    
                    item->metric = 16;
                    triggered_update(item);
                    pthread_mutex_lock(&item->timeout.change_time); 
                    item->timeout.valid = false;
                    remove_route_table(item);
                    pthread_mutex_unlock(&item->timeout.change_time); 
                }
                else 
                {
                    if (item->garbage.valid) 
                    {
                        pthread_mutex_lock(&item->garbage.change_time); 
                        item->garbage.valid = false;
                        pthread_mutex_unlock(&item->garbage.change_time); 
                    }

                    item->metric = re->metric + cost;
                    item->next_hop = nexthop;
                    item->interfc = interfc;
                    pthread_mutex_lock(&item->timeout.change_time); 
                    item->timeout.timer.tv_sec = TIMEOUT;  
                    pthread_mutex_unlock(&item->timeout.change_time);  
                }
            }
        }
    }

    else if (re->metric + cost <= 15) //add a new item
    {   
        struct Route_Table *node = (struct Route_Table*)malloc(sizeof(struct Route_Table));
        //initialize the node
        node->address = re->destination;        
        node->next_hop = nexthop;
        node->interfc = interfc;
        
        node->metric = re->metric + cost;  
        node->timeout.timer.tv_sec = TIMEOUT; 
        node->timeout.fun_ptr = &route_table_timeout;
        node->timeout.args = node; 
        node->timeout.valid = true;
        pthread_mutex_init(&node->timeout.change_time, NULL);

        node->garbage.timer.tv_sec = GARBAGE;
        node->garbage.fun_ptr = &garbage_collect;
        node->garbage.args = node; 
        node->garbage.valid = false;
        pthread_mutex_init(&node->garbage.change_time, NULL);

        set_time(&node->timeout, &node->timeout.timer_thread);
       
        item = table ->next;  
        prior = table ;

        while (item != NULL)           
        {
            if (item->address >= node->address) break;

            prior = item;
            item = item->next;
        }

        node->next = item;              
        prior->next = node;
    }    

    pthread_mutex_unlock(&access_route_table);       //unlock
   
}

void print_route_table()  //print route table
{
    struct Route_Table *item = table ->next;
    pthread_mutex_lock(&access_route_table); 
    printf("\n%d no. Router\n", self.id);
    printf("%-7s%-6s%-8s%-9s%-9s%-10s\n","Src","Dest", "Metric", "NextHop", "Timeout", "Garbage");
    while(item != NULL)
    {
        
        printf("%-7d%-6d%-8d%-9d%-9ld%-10ld\n",item->interfc, item->address, item->metric, item->next_hop, item->timeout.timer.tv_sec, item->garbage.timer.tv_sec);
        
        item = item->next;
    }
    pthread_mutex_unlock(&access_route_table); 
    printf("---------------------------------------------------- \n");
}

//msg decode and validate

void decode_packet(char* packet, int size, struct Interface* interface)
{
    struct Header rh;
    struct Body re;
    int i = 4;
    bool drop = false;
    if ((size - 4) % 20 || size < 4)
    {
        printf("pkt size error, drop\n");
    }
    else
    {
        memcpy(&rh, (void*)packet, sizeof(rh));
        while (size - i > 0)
        {
            memcpy(&re, (void*)packet + i, sizeof(re));
            i += sizeof(re);
        }
        if (!drop)
        {
            i = 4;
            while (size - i > 0)
            {
                memcpy(&re, (void*)packet + i, sizeof(re));
                add_route_table(&re, rh.id, interface->port, interface->dest->metric);
                
                i += sizeof(re);
            }
        }
    }   
}

bool check_receive_match(int port) 
{
    bool result = false;
    for (int i = 0; i < self.onum; i++)
    {
        if (port == self.output[i].port)
        {
            result = true;
            break;
        }
    }
    return result;
}
bool check_metric(int metric)
{
    return (metric >= 0 && metric <= 16);
}

void listen_port(struct Interface *interface) 
{
    struct timeval recv_timeout;   
    interface->sockfd = socket(AF_INET,SOCK_DGRAM,0);    
    
    fd_set recvfd, sendfd;

    struct sockaddr_in local, remote; 
    local.sin_family = AF_INET;          
    local.sin_addr.s_addr = INADDR_ANY;   
    local.sin_port = htons(interface->port);             

    socklen_t remote_len;

    char buf[2000];

    int i, rc, remote_port;

    rc = bind(interface->sockfd,(struct sockaddr *)&local,sizeof(local)); 
    if(rc == -1) { 
        perror("bind");
        exit_program();
    }

    while(!kill) 
    {
        FD_ZERO(&recvfd);        
        FD_SET(interface->sockfd, &recvfd); 
        FD_ZERO(&sendfd);
        FD_SET(interface->sockfd, &sendfd); 

        recv_timeout.tv_sec = TIMEOUT;
        recv_timeout.tv_usec = 0;
        switch(select(interface->sockfd + 1, &recvfd, NULL, NULL, &recv_timeout))
        {
            case 0:
                printf("TIMEOUT: Listening on port %d\n", interface->port);
                break;
            default:  
                if (FD_ISSET(interface->sockfd, &recvfd))
                {
                    rc = recvfrom(interface->sockfd, buf, 2000, 0, (struct sockaddr *)&remote, &remote_len);
                    
                    if (rc == -1) {
                        printf("recvfrom error");
                    }      
                    remote_port = ntohs(remote.sin_port); 
                    if (check_receive_match(remote_port)) 
                    {
                        struct Neighbour *ne; 
                        for(i = 0; i < self.onum; i++)
                        {
                            if (self.output[i].port == remote_port)
                            {
                                ne = &self.output[i];
                                break;
                            }
                        }
                        interface->dest = ne;
                        interface->got_ne = true;
                        decode_packet(buf, rc, interface);  
                    }
                }
        }
    }
    close(interface->sockfd);  
}

void print_bytes(unsigned char *bytes, size_t num_bytes) 
{
    
  for (size_t i = 0; i < num_bytes; i++) {
    printf("%*u ", 3, bytes[i]);
  }
  printf("\n");
}


void* listen_process(void *argv)  //listener
{
    
    struct Interface *interface = (struct Interface *)argv;    

    listen_port(interface);       

    pthread_exit(0);
}

void* update_process()  
{
    while(!kill)
    {
        for (int i = 0; i < self.inum; i++)
        {
            pthread_mutex_lock(&access_route_table); 
            send_update(&self.input[i], NULL);
            pthread_mutex_unlock(&access_route_table); 
        }

        print_route_table(); 
        srand(time(NULL) + self.id);
        sleep (UPDATE);          
    }
    pthread_exit(0);
}


void *PrintHello(void *vargp)
{
    sleep(1);
    printf("Start from Thread \n");
    return NULL;
}


int main(int argc, char **argv) 
{
    //initializing
    self.id = -1;
    self.inum = 0;
    self.onum = 0;
    self.id_status = false;
    self.input = NULL;
    self.output = NULL;
    table  = (struct Route_Table*)malloc(sizeof(struct Route_Table));
    table ->next = NULL;
    table ->address = self.id;
    table ->metric = 0;
    table ->next_hop = 0;
    table ->interfc = 0;
    showlog = true;
    readfile(argv[1], &self);
    showlog = false;
    table ->address = self.id;
    pthread_mutex_t access_route_table = PTHREAD_MUTEX_INITIALIZER;

    if (pthread_create(&updater,NULL,update_process, NULL) != 0)
    {
        exit_program();
    }

    for(int i=0; i<self.inum; i++) 
    {
        if (pthread_create(&self.input[i].listener,NULL,listen_process,&self.input[i]) != 0)
        {
            exit_program();
        }
    }  

    exit_program();     
    return 0;    
}


