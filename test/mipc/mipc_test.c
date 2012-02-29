#include "inc.h"

void test_create_grp(void)
{
  message dummy_msg;
  _syscall(MIPC_PROC_NR, MIPC_CREATE_GRP, &dummy_msg);
}

/* SEF functions and variables. */
FORWARD _PROTOTYPE( void sef_local_startup, (void) );

/*===========================================================================*
 *				main					     *
 *===========================================================================*/
int main(void)
{
	/* SEF local startup. */
	sef_local_startup();

	/* Run all the tests. */
        test_create_grp();
	return 0;
}


/*===========================================================================*
 *			       sef_local_startup			     *
 *===========================================================================*/
PRIVATE void sef_local_startup()
{
  /* Let SEF perform startup. */
  sef_startup();
}

