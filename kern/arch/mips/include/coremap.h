// TODO: coremap interface goes here

struct cm_entry {
	v_addr vm_addr;    // The vm translation of the physical address
	bool is_kernel;    // Note if this is a kernel page or not
	bool allocated;    // Note if the physical address is allocated or not
	bool has_next;     // Indicating that we have a cross-page allocation. Only used for the kernel???
	bool busy;         // If the page is in the process of switching
	int pid;           // The process who owns this memory. TODO: use addr ptr instead
	/* Note that the bool can be combined into a single byte, or with the v_addr to save space */
};

struct vnode *back_store;
struct bitmap *disk_map;

int find_free_page(void);

void cm_bootstrap(void);

