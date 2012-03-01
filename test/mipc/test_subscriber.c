#include "inc.h"
#include "manager.h"

void main(void)
{
  /* fake one */
  char* name = "CS551";
  char* msg;

  int group_id = declare_subscriber(name);

  if(group_id == -1)
  {
    perror("Group doesn't exist!");
    return;
  }

  msg = retrieve_msg();

  printf("I got a msg:%s\n", msg);
  return;
}
