/* General headers */
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>
#include <asm/pgalloc.h>

/* File IO-related headers */
#include <linux/fs.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

/* Sleep and timer headers */
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/pci.h>

#include "../common.h"

/* Simple licensing stuff */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Rodriguez");
MODULE_DESCRIPTION("Project 2, CSE 330 Spring 2024");
MODULE_VERSION("0.01");

/* Calls which start and stop the ioctl teardown */
bool memalloc_ioctl_init(void);
void memalloc_ioctl_teardown(void);

/* Project 2 Solution Variable/Struct Declarations */
#define MAX_PAGES           4096
#define MAX_ALLOCATIONS     100

/* Page table allocation helper functions defined in kmod_helper.c */
pud_t*  memalloc_pud_alloc(p4d_t* p4d, unsigned long vaddr);
pmd_t*  memalloc_pmd_alloc(pud_t* pud, unsigned long vaddr);
void    memalloc_pte_alloc(pmd_t* pmd, unsigned long vaddr);

#if defined(CONFIG_X86_64)
    #define PAGE_PERMS_RW		PAGE_SHARED
    #define PAGE_PERMS_R		PAGE_READONLY
#else
    #define PAGE_PERMS_RW		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_WRITE)
    #define PAGE_PERMS_R		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_RDONLY)
#endif
static dev_t                dev = 0;
static struct class*        virtdev_class;
static struct cdev          virtdev_cdev;
struct alloc_info           alloc_req;
struct free_info            free_req;
struct ioctl_data {
	int number;
};
int allocation = 0;
int allocate_memory(unsigned long vaddr, bool write)
{
	pgd_t *pgd;
    	p4d_t *p4d;
    	pud_t *pud;
    	pmd_t *pmd;
    	pte_t *pte;

    /* Return pointer of the PGD. mm is the mm_struct of the process, address is the logical 
       address in the virtual memory space*/
    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd)) {
        printk("Error: pgd should always be mapped (something is really wrong!).\n");
        //goto out;
        return 0;
    }
    printk("PGD is allocated. \n");

    /* Return pointer to the P4D. pgd is the pointer of PGD, address is the logical address in 
       the virtual memory space.*/
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk("No P4D allocated; page must be unmapped.");
	memalloc_pud_alloc(p4d, vaddr);
        p4d = p4d_offset(pgd, vaddr);
    }
    printk("P4D is allocated. \n");

    /* Return pointer to the PUD. p4d is the pointer of P4D, address is the logical address in 
       the virtual memory space.*/
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        printk("No PUD allocated; page must be unmapped.");
        memalloc_pmd_alloc(pud, vaddr);
        pud = pud_offset(p4d, vaddr);
    }
    printk("PUD is allocated. \n");


    /* Return pointer to the PMD. pud is the pointer of PUD, address is the logical address in 
       the virtual memory space.*/
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        printk("No PMD allocated; page must be unmapped.");
        memalloc_pte_alloc(pmd, vaddr);
        pmd = pmd_offset(pud, vaddr);
    }
    printk("PMD is allocated. \n");

    /* Return pointer to the PTE. pmd is the pointer of PMD, address is the logical address in 
       the virtual memory space*/
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) {
        printk("No PTE allocated; page must be unmapped.");
        //goto out;
    }
    printk("PTE is allocated. \n");

    
    
     	gfp_t gfp = GFP_KERNEL_ACCOUNT ;
	void *virt_addr = (void*)get_zeroed_page(gfp) ;
	if (!virt_addr) {
		printk (" Failed to get a page from the freelist \ n ") ;
		return - ENOMEM ;
	}

    // Get the physical address of the page
    unsigned long paddr = __pa(virt_addr);
    
    // Maping a page to PTE with read - only permissions
    // PAGE_SHIFT is defined within the kernel (12 - bits )
    // PAGE_PERMS_R and PAGE_PERMS_RW are defined in memalloc . c
	
	if (write)
	{
  		set_pte_at(current->mm,vaddr,pte, pfn_pte((paddr >> PAGE_SHIFT), PAGE_PERMS_RW));
  	}
  	else
  	{
  		set_pte_at(current->mm,vaddr,pte, pfn_pte((paddr >> PAGE_SHIFT), PAGE_PERMS_R));
  	}
  	if (pte_present(*pte)) {
    	printk("Page is mapped.");
        return 0;
    }
    printk("Page is not mapped. \n");    // Get a free page for the process
    return -1;
out:
    return 0;
}

//page check
static int page_check(unsigned long vaddr)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    /* Return pointer of the PGD. mm is the mm_struct of the process, address is the logical 
       address in the virtual memory space*/
    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd)) {
        printk("Error: pgd should always be mapped (something is really wrong!).\n");
        goto out;
    }
    printk("PGD is allocated. \n");

    /* Return pointer to the P4D. pgd is the pointer of PGD, address is the logical address in 
       the virtual memory space.*/
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk("No P4D allocated; page must be unmapped.");
        goto out;
    }
    printk("P4D is allocated. \n");

    /* Return pointer to the PUD. p4d is the pointer of P4D, address is the logical address in 
       the virtual memory space.*/
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        printk("No PUD allocated; page must be unmapped.");
        goto out;
    }
    printk("PUD is allocated. \n");


    /* Return pointer to the PMD. pud is the pointer of PUD, address is the logical address in 
       the virtual memory space.*/
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        printk("No PMD allocated; page must be unmapped.");
        goto out;
    }
    printk("PMD is allocated. \n");

    /* Return pointer to the PTE. pmd is the pointer of PMD, address is the logical address in 
       the virtual memory space*/
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) {
        printk("No PTE allocated; page must be unmapped.");
        goto out;
    }
    printk("PTE is allocated. \n");

    if (pte_present(*pte)) {
    	printk("Page is mapped.");
        return -1;      //for some reason the goto out; portion messes it up, so return -1 works
    }
    printk("Page is not mapped. \n");    // Get a free page for the process
    
    gfp_t gfp = GFP_KERNEL_ACCOUNT;
    void *virt_addr = (void*) get_zeroed_page(gfp);
    if (!virt_addr) {
        printk(KERN_ERR "Failed to allocate memory using vmalloc\n");
        return -ENOMEM;
    }

    // Get the physical address of the page
    unsigned long paddr = __pa(virt_addr);

out:
    return 0;
}

/* IOCTL handler for vmod. */
static long memalloc_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    switch (cmd)
    {
    case ALLOCATE:    	
        /* allocate a set of pages */
        if (copy_from_user((void*)&alloc_req, (void*)arg, sizeof(struct alloc_info)))
        {
        	return -1;
        }
        if (page_check(alloc_req.vaddr) == -1)
        {
        	return -1;
        }
        if (alloc_req.num_pages > 4096)
        {
        	return -2;
        }
        allocation++;
        if (allocation > 100)
        {
        	return -3;
        }
        printk("IOCTL: alloc(%lx, %d, %d)\n", alloc_req.vaddr, alloc_req.num_pages, alloc_req.write);
        break;
    case FREE:    	
        /* free allocated pages */
        if (copy_from_user((void*)&free_req, (void*)arg, sizeof(struct free_info)))
        {
        	return -1;
        }
   
    	printk("IOCTL: free(%lx)\n", free_req.vaddr);
    	break;   
    default:
        printk("Error: incorrect IOCTL command.\n");
        return -1;
    }
    for (int i = 0; i < alloc_req.num_pages; ++i)
    {
    	unsigned long vaddr = alloc_req.vaddr+ 4096*i;
    	int res = allocate_memory(vaddr, alloc_req.write);
    	if (res == -1)
    	{
    		printk("Unable to allocate memory\n");
    	}
    }
    return 0;
}

/* Required file ops. */
static struct file_operations fops = 
{
    .owner          = THIS_MODULE,
    .unlocked_ioctl = memalloc_ioctl,
};

static int __init memalloc_module_init(void) {

    /* Allocate a character device. */
    if (alloc_chrdev_region(&dev, 0, 1, "memalloc") < 0) {
        printk("error: couldn't allocate chardev region.\n");
        return -1;
    }
    printk("[*] Allocated chardev.\n");

    /* Initialize the chardev with my fops. */
    cdev_init(&virtdev_cdev, &fops);

    if (cdev_add(&virtdev_cdev, dev, 1) < 0) {
        printk("[x] Couldn't add memalloc cdev.\n");
        goto cdevfailed;
    }
    printk("[*] Allocated cdev.\n");

    if ((virtdev_class = class_create("memalloc_class")) == NULL) {
        printk("[X] couldn't create class.\n");
        goto cdevfailed;
    }
    printk("[*] Allocated class.\n");

    if ((device_create(virtdev_class, NULL, dev, NULL, "memalloc")) == NULL) {
        printk("[X] couldn't create device.\n");
        goto classfailed;
    }
    printk("[*] Virtual device added.\n");

    return 0;

classfailed:
    class_destroy(virtdev_class);
cdevfailed:
    unregister_chrdev_region(dev, 1);

    return -1;
}

static void __exit memalloc_module_exit(void) {
    /* Destroy the classes too (IOCTL-specific). */
    if (virtdev_class) {
        device_destroy(virtdev_class, dev);
        class_destroy(virtdev_class);
    }
    cdev_del(&virtdev_cdev);
    unregister_chrdev_region(dev,1);
    printk("[*] Virtual device removed.\n");
}

/* Initialize the module for IOCTL commands */
bool memalloc_ioctl_init(void) {
    return false;
}

void memalloc_ioctl_teardown(void) {
    /* Destroy the classes too (IOCTL-specific). */
}

module_init(memalloc_module_init);
module_exit(memalloc_module_exit);
