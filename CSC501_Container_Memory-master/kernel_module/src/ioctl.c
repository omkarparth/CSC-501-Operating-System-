//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2018
//// Project 2: Omkar Parkhe, ojparkhe; Snehal Sonvane, sssonvan
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

#include "memory_container.h"

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


typedef struct process_list
{
	struct task_struct *process;
	struct process_list* next;
}process_list;

typedef struct object_list
{
	int oid;
	unsigned long pfn;
	char *virt_addr;
	struct object_list *next;
}object_list;

typedef struct lock_list
{
	int oid;
	struct mutex mutex_lock;
	struct lock_list *next;
}lock_list;

typedef struct container_list
{
	int cid;
	object_list* olist;
	process_list* list;
	lock_list* llist;
	struct container_list* next;
}container_list;

container_list* head = NULL;

static DEFINE_MUTEX(mutex);

object_list* findobject(unsigned long id, container_list *container)
{
	container_list *temp = container, *result = NULL;
	object_list *objectHead = temp->olist;
	while(objectHead!=NULL)
	{
		if(objectHead->oid == id)
		{
			result = objectHead;
			return result;
		}
		objectHead = objectHead->next;
	}
	return NULL;
}


container_list* findcontainer(struct task_struct *c)
{
	
	container_list *temp = head, *result = NULL;
	while(temp!=NULL)
	{
		process_list *processHead = temp->list;
		while(processHead!=NULL)
		{
			if(processHead->process->pid == c->pid)
			{
				result = temp;
				return result;
			}
			processHead = processHead->next;
		}
		temp = temp->next;
	}
	return NULL;
}


lock_list* findlock(unsigned long id, container_list *container)
{
	container_list *temp = container, *result = NULL;
	lock_list *lockHead = temp->llist;
	while(lockHead!=NULL)
	{
		if(lockHead->oid == id)
		{
			result = lockHead;
			return result;
		}
		lockHead = lockHead->next;
	}
	return NULL;
}


void print_container(void)
{
	container_list *current_container = head;
	process_list *plist = NULL;
	object_list *olist = NULL;
	while(current_container != NULL)
	{
		//printk("\nContainer: %d",current_container->cid);
		plist = current_container->list;
		olist = current_container->olist;
		//printk("\nProcess list:");
		while(plist != NULL)
		{
			//printk("%d \t",plist->process->pid);
			plist = plist->next;
		}
		//printk("\nObject list:");
		while(olist != NULL)
		{
			//printk("%d \t",olist->oid);
			olist = olist->next;
		}
		current_container = current_container->next;
	}
}


int memory_container_mmap(struct file *filp, struct vm_area_struct *vma)
{
	mutex_lock(&mutex);
	//printk("\nEntering mmap");
	container_list *container = findcontainer(current);
	//printk("\nFound container %d", container->cid);
	object_list *o = findobject(vma->vm_pgoff, container);
	//printk("\nPage Offset: %d", vma->vm_pgoff);

	if(o == NULL)
	{
		object_list *new, *current_object = container->olist, *prev = NULL;
		unsigned long oid = vma->vm_pgoff;
		int flag = 0;
		
		while(current_object!=NULL)
		{		
			prev = current_object;
			current_object = current_object->next;
		}

		char *data = (char*)kcalloc(1, (vma->vm_end-vma->vm_start)*sizeof(char), GFP_KERNEL);
		unsigned long pfn = virt_to_phys((void*)data)>>PAGE_SHIFT;
		remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end-vma->vm_start, vma->vm_page_prot);
		//printk("\nCreated Object with ID: %d and PFN: %d", vma->vm_pgoff, pfn);
		
		new = (object_list *)kmalloc(sizeof(object_list), GFP_KERNEL);
		new->pfn = pfn;
		new->virt_addr = data;
		new->oid = vma->vm_pgoff;
		new->next = NULL;
		if(prev == NULL)
			container->olist = new;
		else
			prev->next = new;
		//printk("Object added to the list");
	}

	else if(o->oid == vma->vm_pgoff)
	{
		//printk("\nFound Object with ID: %d and PFN: %d", o->oid, o->pfn);
		remap_pfn_range(vma, vma->vm_start, o->pfn, vma->vm_end-vma->vm_start, vma->vm_page_prot);
		mutex_unlock(&mutex);
		return 0;
	}

	//printk("\nExiting mmap");
	print_container();
	mutex_unlock(&mutex);
	return 0;
}


int memory_container_lock(struct memory_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);
	//printk("\nEntering lock");
	container_list *container = findcontainer(current);
	//printk("\nFound container %d", container->cid);
	struct memory_container_cmd c;
	copy_from_user(&c,user_cmd, sizeof(c)); //fetch container Id from user space
	unsigned long object_id = (int)c.oid;
	//printk("\nObject ID: %d", object_id);
	lock_list *lock = findlock(object_id, container);
	if(lock == NULL)
	{
		//create lock
		lock_list *new, *current_lock = container->llist, *prev = NULL;
		
		while(current_lock!=NULL)
		{		
			prev = current_lock;
			current_lock = current_lock->next;
		}

		new = (lock_list *)kmalloc(sizeof(lock_list), GFP_KERNEL);
		new->oid = object_id;
		mutex_init(&new->mutex_lock);
		new->next = NULL;
		if(prev == NULL)
			container->llist = new;
		else
			prev->next = new;
		lock = new;
		//printk("\nCreated lock");
	}
	print_container();
	mutex_unlock(&mutex);
	//printk("\nProcess %d Mutex is %d", current->pid, mutex_is_locked(&lock->mutex_lock));
	mutex_lock(&lock->mutex_lock);
	//printk("\nProcess %d accquired mutex is %d", current->pid, mutex_is_locked(&lock->mutex_lock));
	//printk("\nExiting lock");
	return 0;
}


int memory_container_unlock(struct memory_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);
	//printk("\nEntering unlock for process: %d", current->pid);
	container_list *container = findcontainer(current);
	struct memory_container_cmd c;
	copy_from_user(&c,user_cmd, sizeof(c)); //fetch container Id from user space
	unsigned long object_id = (int)c.oid;
	lock_list *current_lock = findlock(object_id, container);
	mutex_unlock(&current_lock->mutex_lock);
	mutex_unlock(&mutex);
	//printk("\nExiting unlock");
	return 0;
}


int memory_container_delete(struct memory_container_cmd __user *user_cmd)
{	
	mutex_lock(&mutex);
	//printk("\nEntering delete for process: %d", current->pid);

	container_list *current_container = findcontainer(current);
	process_list *current_process = current_container->list, *prev_process = NULL;
	while(current_process->process->pid != current->pid)
	{
		prev_process = current_process;
		current_process = current_process->next;		
	}
	//printk("\nFound process: %d", current_process->process->pid);	
	if(prev_process == NULL)
		current_container->list = current_process->next;
	else
		prev_process->next = current_process->next;
	kfree(current_process);
	//current_process = NULL;

	//printk("\nExiting delete");
	print_container();
	mutex_unlock(&mutex);
	return 0;
}


void delete_all(void){
	container_list *c=NULL,*current_container = head;
	object_list *o = NULL, *current_object = current_container->olist;
	lock_list *l = NULL, *current_lock = current_container->llist;
	while(current_container!=NULL)
	{
		current_object = current_container->olist;
		current_lock = current_container->llist;
		while(current_object != NULL)
		{
			o = current_object;
			current_object = current_object->next;
			kfree(o->virt_addr);
			o->virt_addr = NULL;
			kfree(o);
			o->virt_addr = NULL;
		}
		while(current_lock != NULL)
		{
			l = current_lock;
			current_lock = current_lock->next;
			kfree(l);
			l = NULL;
		}
		c = current_container;
		current_container = current_container->next;
		kfree(c);
		c = NULL;
	}
}


int memory_container_create(struct processor_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);
	int container_id, object_id;
	struct memory_container_cmd container;
	copy_from_user(&container,user_cmd, sizeof(container)); //fetch container Id from user space
	container_id = (int)container.cid;
	//printk("\nEntering create Pid: %d Tgid: %d Container ID: %d Object ID: %d", current->pid, current->tgid, container_id, object_id);
	if(head == NULL) //create new container if no container exists
	{
		head = (container_list *)kmalloc(sizeof(container_list), GFP_KERNEL); //initialize container list
		head->cid = container_id;
		head->next = NULL;
		process_list *phead = (process_list *)kmalloc(sizeof(process_list), GFP_KERNEL);// initialize container's process list
		phead->next = NULL;
		phead->process = current;
		head->list = phead;
		head->olist = NULL;
		head->llist = NULL;
	}
	else 
	{
		container_list *temp = head, *t = NULL;		
		while(temp!=NULL)
		{		
			if(temp->cid == container_id)
			{
				process_list *phead = temp->list;
				process_list *new = (process_list *)kmalloc(sizeof(process_list), GFP_KERNEL); 
				new->next = NULL;
				new->process = current;

				if(phead == NULL)
				{
					temp->list = new;
				}
				else
				{
					while(phead->next!=NULL)
						phead=phead->next;
 
					phead->next = new; //append process to an existing container
				}
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
			new->olist = NULL;
			new->llist = NULL;
			process_list *phead = (process_list *)kmalloc(sizeof(process_list), GFP_KERNEL);
			phead->next = NULL;
			phead->process = current;
			new->list = phead;
			t->next = new;
		}
	}

	
	//printk("\nExiting create");
	print_container();
	mutex_unlock(&mutex);
	return 0;
}


int memory_container_free(struct memory_container_cmd __user *user_cmd)
{
	mutex_lock(&mutex);
	struct memory_container_cmd c;
	copy_from_user(&c,user_cmd, sizeof(c)); //fetch container Id from user space
	unsigned long object_id = (int)c.oid;
	container_list *container = findcontainer(current);
	//printk("\nEntering free for process %d, container %d and object %d", current->pid, container->cid, object_id);
	container_list *current_container = container, *prev_container = NULL;
	object_list *current_object, *prev_object = NULL;

	object_list *objectHead = current_container->olist;
	while(objectHead!=NULL)
	{
		if(objectHead->oid == object_id)
		{
			current_object = objectHead;				
			if(prev_object == NULL)
				current_container->olist = objectHead->next;
			else
				prev_object->next = objectHead->next;
			kfree(current_object->virt_addr);
			current_object->virt_addr = NULL;
			kfree(current_object);
			current_object = NULL;
			break;
		}
		prev_object = objectHead;
		objectHead = objectHead->next;
	}
	//printk("\nExiting free");
	print_container();	
	mutex_unlock(&mutex);
	return 0;
}


/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int memory_container_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
    case MCONTAINER_IOCTL_CREATE:
        return memory_container_create((void __user *)arg);
    case MCONTAINER_IOCTL_DELETE:
        return memory_container_delete((void __user *)arg);
    case MCONTAINER_IOCTL_LOCK:
        return memory_container_lock((void __user *)arg);
    case MCONTAINER_IOCTL_UNLOCK:
        return memory_container_unlock((void __user *)arg);
    case MCONTAINER_IOCTL_FREE:
        return memory_container_free((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
