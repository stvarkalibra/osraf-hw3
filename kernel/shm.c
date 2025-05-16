#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "vm.h"

#define SHM_BASE (KERNBASE / 2)

struct shm_table shm_table;

int sys_shm_open(void) {
	char *name;
	if (argstr(0, &name) < 0)
		return -1;

	struct proc *curproc = myproc();
	acquire(&shm_table.lock);

	for (int i = 0; i < 16; i++) {
		if (curproc->otvoreni_objekti[i].brojac_procesa > 0 &&
			strncmp(curproc->otvoreni_objekti[i].ime, name, sizeof(curproc->otvoreni_objekti[i].ime)) == 0) {
			int global_index = curproc->otvoreni_objekti[i].global_index;
		shm_table.objects[global_index].brojac_procesa++;
		curproc->otvoreni_objekti[i].brojac_procesa = shm_table.objects[global_index].brojac_procesa;
		release(&shm_table.lock);
		return i;
			}
	}

	for (int i = 0; i < 64; i++) {
		if (shm_table.objects[i].brojac_procesa > 0 &&
			strncmp(shm_table.objects[i].ime, name, sizeof(shm_table.objects[i].ime)) == 0) {
			for (int j = 0; j < 16; j++) {
				if (curproc->otvoreni_objekti[j].brojac_procesa == 0) {
					curproc->otvoreni_objekti[j] = shm_table.objects[i];
					shm_table.objects[i].brojac_procesa++;
					curproc->otvoreni_objekti[j].brojac_procesa = shm_table.objects[i].brojac_procesa;
					curproc->otvoreni_objekti[j].global_index = i;
					release(&shm_table.lock);
					return j;
				}
			}
			release(&shm_table.lock);
			return -1;
			}
	}

	int global_index = -1;
	for (int i = 0; i < 64; i++) {
		if (shm_table.objects[i].brojac_procesa == 0) {
			global_index = i;
			break;
		}
	}
	if (global_index == -1) {
		release(&shm_table.lock);
		return -1;
	}

	int lokalni_index = -1;
	for (int i = 0; i < 16; i++) {
		if (curproc->otvoreni_objekti[i].brojac_procesa == 0) {
			lokalni_index = i;
			break;
		}
	}
	if (lokalni_index == -1) {
		release(&shm_table.lock);
		return -1;
	}

	memset(&shm_table.objects[global_index], 0, sizeof(struct shm_object));
	safestrcpy(shm_table.objects[global_index].ime, name, sizeof(shm_table.objects[global_index].ime));
	shm_table.objects[global_index].velicina = 0;
	shm_table.objects[global_index].brojac_procesa = 1;

	curproc->otvoreni_objekti[lokalni_index] = shm_table.objects[global_index];
	curproc->otvoreni_objekti[lokalni_index].global_index = global_index;

	release(&shm_table.lock);
	return lokalni_index;
}

int sys_shm_trunc(void) {
	int shm_od, size;
	if (argint(0, &shm_od) < 0 || argint(1, &size) < 0)
		return -1;

	if (shm_od < 0 || shm_od >= 16 || size <= 0)
		return -1;

	struct proc *curproc = myproc();
	acquire(&shm_table.lock);

	struct shm_object *obj = &curproc->otvoreni_objekti[shm_od];
	if (obj->brojac_procesa == 0) {
		release(&shm_table.lock);
		return -1;
	}

	if (obj->velicina > 0) {
		release(&shm_table.lock);
		return -1;
	}

	int num_pages = (size + PGSIZE - 1) / PGSIZE;
	char *nove_stranice[32];
	if (num_pages > 32) {
		release(&shm_table.lock);
		return -1;
	}

	for (int i = 0; i < num_pages; i++) {
		nove_stranice[i] = kalloc();
		if (nove_stranice[i] == 0) {
			for (int j = 0; j < i; j++) {
				kfree(nove_stranice[j]);
			}
			release(&shm_table.lock);
			return -1;
		}
		memset(nove_stranice[i], 0, PGSIZE);
	}

	for (int i = 0; i < num_pages; i++) {
		obj->niz_adresa_stranica[i] = nove_stranice[i];
	}
	obj->velicina = num_pages * PGSIZE;

	for (int i = 0; i < 64; i++) {
		if (strncmp(shm_table.objects[i].ime, obj->ime, sizeof(obj->ime)) == 0) {
			shm_table.objects[i] = *obj;
			break;
		}
	}

	release(&shm_table.lock);
	return 0;
}

int sys_shm_map(void) {
	int shm_od;
	char **user_ptr;
	int flags;

	if (argint(0, &shm_od) < 0)
		return -1;
	if (argptr(1, (char**)&user_ptr, sizeof(void *)) < 0)
		return -1;
	if (argint(2, &flags) < 0)
		return -1;
	if (shm_od < 0 || shm_od >= 16)
		return -1;

	struct proc *curproc = myproc();
	struct shm_object *obj = &curproc->otvoreni_objekti[shm_od];

	if (obj->brojac_procesa == 0 || obj->velicina == 0)
		return -1;

	uint va = SHM_BASE + shm_od * PGSIZE * 32;

	int perm = PTE_U;
	if (flags == O_RDWR)
		perm |= PTE_W;

	int num_pages = obj->velicina / PGSIZE;
	for (int i = 0; i < num_pages; i++) {
		if (mappages(curproc->pgdir, (char*)(va + i * PGSIZE), PGSIZE,
			V2P(obj->niz_adresa_stranica[i]), perm) < 0) {
			for (int j = 0; j < i; j++) {
				unmap(curproc->pgdir, (void*)(va + j * PGSIZE), i);
			}
			return -1;
			}
	}

	curproc->virtuelne_adrese[shm_od] = (char*)va;
	curproc->flags[shm_od] = flags;
	if (copyout(curproc->pgdir, (uint)user_ptr, (char*)&va, sizeof(void *)) < 0)
		return -1;

	return 0;
}

int sys_shm_close(void) {
	int shm_od;
	if (argint(0, &shm_od) < 0)
		return -1;

	struct proc *curproc = myproc();
	if (shm_od < 0 || shm_od >= 16)
		return -1;

	acquire(&shm_table.lock);
	struct shm_object *obj = &curproc->otvoreni_objekti[shm_od];

	if (curproc->virtuelne_adrese[shm_od] != '\0') {
		unmap(curproc->pgdir, (void*)curproc->virtuelne_adrese[shm_od],
			  curproc->otvoreni_objekti[shm_od].velicina / PGSIZE);
		curproc->virtuelne_adrese[shm_od] = '\0';
	}
	obj->brojac_procesa--;

	if (obj->brojac_procesa == 0) {
		for (int i = 0; i < 64; i++) {
			if (strncmp(shm_table.objects[i].ime, obj->ime, sizeof(obj->ime)) == 0) {
				memset(&shm_table.objects[i], 0, sizeof(struct shm_object));
				break;
			}
		}
	}

	memset(&curproc->otvoreni_objekti[shm_od], 0, sizeof(struct shm_object));
	release(&shm_table.lock);
	return 0;
}

void shminit(void) {
	initlock(&shm_table.lock, "shm_table");
	for (int i = 0; i < 64; i++) {
		shm_table.objects[i].brojac_procesa = 0;
	}
}
