#ifndef XV6_VM_H
#define XV6_VM_H

#include "types.h"
#include "proc.h"

// Kernel page directory, defined in vm.c
extern pde_t *kpgdir;

void seginit(void);
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

pde_t* setupkvm(void);
void kvmalloc(void);
void switchkvm(void);
void switchuvm(struct proc *p);
void inituvm(pde_t *pgdir, char *init, uint sz);
int loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz);
int allocuvm(pde_t *pgdir, uint oldsz, uint newsz);
int deallocuvm(pde_t *pgdir, uint oldsz, uint newsz);
void freevm(pde_t *pgdir);
void clearpteu(pde_t *pgdir, char *uva);
pde_t* copyuvm(pde_t *pgdir, uint sz);

void unmap(pde_t *pgdir, void *uva, uint npages);

#endif
