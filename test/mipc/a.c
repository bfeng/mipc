#include <stdio.h>
#include <string.h>

#define _MIPC_MAX_GROUPS 32
#define _MIPC_MSG_SIZE 100

typedef struct{
    char name[32];  //name of the interest group max size of 32 
    int gid;        // interest group id
    int subscribers[20]; // List of subscribers PIDs
    int subcounter;
    int publishers[10]; // List of publishers PID
    int pubcounter;
    char messagelist[100][_MIPC_MSG_SIZE]; // string array of the group messages 
    char messagebuffer[5][_MIPC_MSG_SIZE]; // string array of message buffer
    int msgcounter; // current message counter
    int bufcounter; // current buffer counter
} GROUP;

GROUP groups[_MIPC_MAX_GROUPS];
int grpCount=0;
int grpId=100;

int cleargroup(void);
int findgrpname(char []);
int findgrpgid(int gid);
int create_interest_group(char []);
int declare_publisher(int , int );
int declare_subscriber(int , int);
int chk_publisher(int , int);
int publish_msg(int , int , char []);
void publish(int );
int chk_subscriber(int ,int);
char* subscribe_msg(int , int);



void main(){
    printf("Hello\n");
    
    printf("\n%d",create_interest_group("film"));
    printf("\n%d",declare_publisher(3,100));
    printf("\n%d",declare_publisher(4,100));
    printf("\n%d",declare_publisher(5,100));
    printf("\n%d",declare_publisher(2,100));
    
    printf("\n%d",declare_subscriber(2,100));
    printf("\n%d",publish_msg(2,100,"35mm"));
    printf("\n%s",subscribe_msg(2,100));
    
}

int cleargroup(){
}

int findgrpname(char name[]){
    int grpCounter;
    if (grpCount==0) return -1; 
    else
    for(grpCounter=1;grpCounter<=grpCount;grpCounter++){
        if (strcmp(groups[grpCounter].name , name) == 0) return grpCounter;
    }
    return -1;
}
int findgrpgid(int gid){
    int grpCounter;
    if (grpCount==0) return -1; 
    else
        for(grpCounter=0;grpCounter<grpCount;grpCounter++){
            if (groups[grpCounter].gid== gid) return grpCounter;
        }
    return -1;
}

int create_interest_group(char name[]){
    
    if (grpCount==_MIPC_MAX_GROUPS) return -100;
    
    //printf("grpCount is %d and grpId is %d",grpCount,grpId);
    //printf("\nGroup Id set to %d" , grpId);
    strcpy(groups[grpCount].name,name);
    groups[grpCount].gid=grpId;
    groups[grpCount].msgcounter=0;
    groups[grpCount].bufcounter=0;
    groups[grpCount].subcounter=0;
    groups[grpCount].pubcounter=0;
    grpId++;
    grpCount++;
    
    return 1;
}

int declare_publisher(int pid, int gid){
    int grpnum=findgrpgid(gid);
    
    if (grpnum<0) return -1;
    
    if ( groups[grpnum].pubcounter == 10 ) return -10;
    
    groups[grpnum].publishers[groups[grpnum].pubcounter]=pid;
    groups[grpnum].pubcounter++;
    
    return 1;
}


int declare_subscriber(int pid, int gid){
    int grpnum=findgrpgid(gid);
    
    if (grpnum<0) return -1;
    
    if ( groups[grpnum].subcounter == 20 ) return -20;
    
    groups[grpnum].subscribers[groups[grpnum].subcounter]=pid;
    groups[grpnum].subcounter++;
    
    return 1;
}

int chk_publisher(int pid,int gid){
    int grpnum=findgrpgid(gid);
    int pubCounter;
    
    if (grpnum<0) return -1;
    
    for (pubCounter=0;pubCounter<=groups[grpnum].pubcounter;pubCounter++)
        if (groups[grpnum].publishers[pubCounter]==pid) {
            printf("Found publisher");
            return 1;
            
        }else{
            printf("Dint find publisher");
        };
    
    return -10;
}

int publish_msg(int pid, int gid, char message[]){
    int grpnum=findgrpgid(gid);
    int pubnum=chk_publisher(pid,gid);
    
    if (grpnum<0) return -1;
    if (pubnum<0) return -10;

    if (groups[grpnum].bufcounter==5) return -5;
    
    strcpy(groups[grpnum].messagebuffer[groups[grpnum].bufcounter],message);
    groups[grpnum].bufcounter++;
    
    strcpy(groups[grpnum].messagelist[groups[grpnum].msgcounter],message);
    groups[grpnum].msgcounter++;
    
    return 1;
    
}

int chk_subscriber(int pid,int gid){
    int grpnum=findgrpgid(gid);
    int subCounter;
    
    if (grpnum<0) return -1;
    
    for (subCounter=0;subCounter<=groups[grpnum].subcounter;subCounter++)
        if (groups[grpnum].subscribers[subCounter]==pid) {
            printf("Found subscriber");
            return 1;
            
        }else{
            printf("Dint find subscriber");
        };
    
    return -10;
}

char* subscribe_msg(int pid, int gid){
    char* message;
    int grpnum=findgrpgid(gid);
    int subnum=chk_subscriber(pid,gid);
    
    if (grpnum<0) return "grp error";
    if (subnum<0) return "sub error";
    
    if (groups[grpnum].msgcounter==0) return "";
    
    message=groups[grpnum].messagelist[groups[grpnum].msgcounter];
     printf("this is where i should get the message %s",groups[grpnum].messagelist[groups[grpnum].msgcounter]);
    return message;
    
}