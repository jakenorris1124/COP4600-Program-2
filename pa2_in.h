extern struct mutex pa2_mutex;
extern struct wait_queue_head wq;
extern struct queue *q;
extern int all_msg_size;

void get_lock(char name[]);
void release_lock(char name[]);

struct queue
{
    struct msgs *top;
    struct msgs *bottom;
};

struct msgs
{
    char *msg;
    int msg_size;
    struct msgs *next;
};