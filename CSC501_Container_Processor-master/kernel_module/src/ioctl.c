//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for Processor Container
//
////////////////////////////////////////////////////////////////////////

// Project 1: Omkar Parkhe, ojparkhe; Snehal Sonvane, sssonvan


#include "processor_container.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>



typedef struct thread_list
{
	struct task_struct *thread;
	struct thread_list* next;
}thread_list;

typedef struct container_list
{
	int cid;
	thread_list* list;
	struct container_list* next;
}container_list;

container_list* head = NULL;


static DEFINE_MUTEX(mutex);

		
/***Function findcontainer() returns the container to which the given thread belongs ***/
container_list* findcontainer(struct task_struct *c)
{
	
	container_list *temp = head, *result = NULL;
	while(temp!=NULL)
	{
		thread_list *threadHead = temp->list;
		while(threadHead!=NULL)
		{
			if(threadHead->thread == c)
			{
				result = temp;
				return result;
			}
			threadHead = threadHead->next;
		}
		temp = temp->next;
	}
	return NULL;
}


/**
 * Delete the task in the container.
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), 
 */


int processor_container_delete(struct processor_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);  
	container_list *container = NULL;
	container_list *temp = head;
	container_list *temporary = NULL;
	thread_list *thead = NULL;
	int flag =0;

	container = findcontainer(current);
	printk("\nEntering delete Pid: %d Tgid: %d Container ID: %d", current->pid, current->tgid, container->cid);

	while(temp!=NULL)
	{
		if(temp->cid == container->cid)
		{
			thead = temp->list;
			temp->list = thead->next;
			kfree(thead); //free memory holding the thread
			thead = NULL;
			if(temp->list == NULL)
			{
				if(temp == head)
				{
					head = head->next;
				}
				else
				{
					temporary->next = temp->next;
				}
				kfree(temp); //free memory holding the container given no of threads=0
				temp = NULL;
				flag = 1;
			}
			break;
		}
		temporary = temp;
		temp = temp->next;	
	}

	mutex_unlock(&mutex);
	if(flag == 0)
	{
		printk("\nDeleting thread %d from container %d and waking up %d", current->pid, container->cid, temp->list->thread->pid);
		wake_up_process(temp->list->thread);  // Waking up next thread in the container's thread queue
	}
	
	

	return 0;
}


/***Pushthreadtoend() appends running thread at the end of the container's thread queue***/

void pushthreadtoend(int cid)
{
	container_list *temp = head;
	thread_list *threadHead=NULL;
	if(temp!=NULL)
	{
		while(temp!=NULL)
		{
			if(temp->cid == cid)
			{
				threadHead=temp->list;
				break;
			}
			temp = temp->next;	
		}
	}
        
	//no action is performed if corresponding container's thread queue is empty or has only one thread  
	if(threadHead==NULL || threadHead->next==NULL)
	{
		return;
	}

	thread_list *newThreadHead = threadHead->next;
	thread_list *node=threadHead;

	while(node->next!=NULL)
		node=node->next; 
	node->next=threadHead;
	threadHead->next=NULL;
	temp->list=newThreadHead;
}

/**
 * Create a task in the corresponding container.
 * external functions needed:
 * copy_from_user(), mutex_lock(), mutex_unlock(), set_current_state(), schedule()
 * 
 * external variables needed:
 * struct task_struct* current  
 */
int processor_container_create(struct processor_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);
	int container_id;
	int flag=0;
	struct processor_container_cmd container;
	copy_from_user(&container,user_cmd, sizeof(container)); //fetch container Id from user space
	container_id = (int)container.cid;
	printk("\nEntering create Pid: %d Tgid: %d Container ID: %d", current->pid, current->tgid, container_id);
	if(head == NULL) //create new container if no container exists
	{
		head = (container_list *)kmalloc(sizeof(container_list), GFP_KERNEL); //initialize container list
		head->cid = container_id;
		head->next = NULL;
		thread_list *thead = (thread_list *)kmalloc(sizeof(thread_list), GFP_KERNEL);// initialize container's thread list
		thead->next = NULL;
		thead->thread = current;
		head->list = thead;
	}
	else 
	{
		container_list *temp = head, *t = NULL;		
		while(temp!=NULL)
		{		
			if(temp->cid == container_id)
			{
				thread_list *thead = temp->list;
				flag=1;
				while(thead->next!=NULL)
					thead=thead->next;
				thread_list *new = (thread_list *)kmalloc(sizeof(thread_list), GFP_KERNEL); 
				new->next = NULL;
				new->thread = current; 
				thead->next = new; //append thread to an existing container
				break;
			}
			t = temp;
			temp = temp->next;
		}
		if(temp==NULL) //creating a new container and appending it to container list
		{
			container_list *new = (container_list *)kmalloc(sizeof(container_list), GFP_KERNEL);
			new->cid = container_id;
			new->next = NULL;
			thread_list *thead = (thread_list *)kmalloc(sizeof(thread_list), GFP_KERNEL);
			thead->next = NULL;
			thead->thread = current;
			new->list = thead;
			t->next = new;
		}
	}

	mutex_unlock(&mutex);
	if(flag == 1)
	{

		printk("\nPutting thread %d to sleep",current->pid);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}

    return 0;
}




/**
 * switch to the next task within the same container
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), set_current_state(), schedule()
 */
/***processor_container_switch() implements Round Robin scheduling within the threads assigned to the same container***/
int processor_container_switch(struct processor_container_cmd __user *user_cmd)
{
	if(current->pid==current->tgid) //ignore switch call for benchmark process
		return 0;
	mutex_lock(&mutex);
	printk("\nEntering switch Pid: %d Tgid: %d ", current->pid, current->tgid);
	container_list *container = findcontainer(current);
	if(container==NULL)
	{
		mutex_unlock(&mutex);
		printk("\nNo switch as there is only one thread in the container");
		return 0;
	}
	printk("Container ID: %d",container->cid);
	pushthreadtoend(container->cid);
	mutex_unlock(&mutex);
	if(container->list->next!=NULL)
	{       
                printk("\nSwitching from %d to %d",current->pid,container->list->thread->pid);
		wake_up_process(container->list->thread); //waking up next thread in the queue
		set_current_state(TASK_INTERRUPTIBLE); //sleeping the current thread
		schedule();
	}
	
    return 0;
}




/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int processor_container_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    
    switch (cmd)
    {
    case PCONTAINER_IOCTL_CSWITCH:
        return processor_container_switch((void __user *)arg);
    case PCONTAINER_IOCTL_CREATE:
        return processor_container_create((void __user *)arg);
    case PCONTAINER_IOCTL_DELETE:
        return processor_container_delete((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
