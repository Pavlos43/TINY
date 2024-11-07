#include "kernel_streams.h"
#include "kernel_proc.h"
#include "kernel_cc.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
	PTCB* ptcb;
  PCB* pcb= CURPROC;

  if(task!=NULL){
    TCB* tcb=spawn_thread(pcb,start_thread_ptcb);
    
    ptcb=initialize_PTCB();
    ptcb->task=task;
    ptcb->argl=argl;
    ptcb->args=args;
    ptcb->tcb=tcb;
    tcb->ptcb=ptcb;
    ptcb->tcb->owner_pcb=pcb;
    
    rlist_push_back(&pcb->ptcb_list,&ptcb->ptcb_list_node);
    pcb->thread_count+=1;
    
    wakeup(ptcb->tcb);  
  }

  return (Tid_t) ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf(){
	return (Tid_t) (cur_thread()->ptcb);
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	
  PTCB* ptcb_id=(PTCB*)tid; //cast so we can work with the tid as a ptcb
  PCB* curpcb= CURPROC;
  rlnode* search_for_ptcb =rlist_find(&curpcb->ptcb_list, ptcb_id, NULL);
  
  if (search_for_ptcb == NULL){
    return -1;  
  }
  

  if(tid==sys_ThreadSelf()){

    return -1;
  }

  if(ptcb_id->detached==1){
    return -1;
  }
        
  ptcb_id->refcount++;

  while(ptcb_id->detached==0 && ptcb_id->exited==0){
       kernel_wait(&ptcb_id->exit_cv,SCHED_USER);
  }

  ptcb_id->refcount--;

  if(ptcb_id->detached==1){//running thread detached
    return -1;
  }
  if (exitval != NULL){
    *exitval=ptcb_id->exitval;
  }

  if (ptcb_id->refcount == 0) {
  rlist_remove(&ptcb_id->ptcb_list_node);
  free(ptcb_id);
  }     
  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	PTCB* ptcb_id=(PTCB*)tid;//cast so we can work with the tid as a ptcb
  PCB* curpcb= CURPROC;
  rlnode* search_for_ptcb =rlist_find(&curpcb->ptcb_list, ptcb_id, NULL);

  if(search_for_ptcb==NULL){
    return -1;
  }
  if(ptcb_id->exited==1){
    return -1;
  }
  
  ptcb_id->detached=1;
  kernel_broadcast(&ptcb_id->exit_cv);
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB* curpcb= CURPROC;
  PCB* initpcb=get_pcb(1);
  TCB* tcb =cur_thread();
  PTCB* curptcb=tcb->ptcb;

  curpcb->thread_count--;
  curptcb->exited=1;
  curptcb->exitval=exitval;

  kernel_broadcast(&curptcb->exit_cv);

  if(curpcb->thread_count==0){
    if(get_pid(curpcb)!=1){
      while(!is_rlist_empty(& curpcb->children_list)) {
      rlnode* child = rlist_pop_front(& curpcb->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
      }
      if(!is_rlist_empty(& curpcb->exited_list)) {
        rlist_append(& initpcb->exited_list, &curpcb->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curpcb->parent->exited_list, &curpcb->exited_node);
      kernel_broadcast(& curpcb->parent->child_exit);
    }
    /* Release the args data */
    if(curpcb->args) {
      free(curpcb->args);
      curpcb->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curpcb->FIDT[i] != NULL) {
        FCB_decref(curpcb->FIDT[i]);
        curpcb->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curpcb->main_thread = NULL;

    while(is_rlist_empty(&curpcb->ptcb_list)==0){
      rlist_pop_front(&curpcb->ptcb_list);
    }

    /* Now, mark the process as exited. */
    curpcb->pstate = ZOMBIE;
  }
  kernel_sleep(EXITED,SCHED_USER);
}

