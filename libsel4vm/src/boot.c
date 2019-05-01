/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <autoconf.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/util.h>

#include <sel4vm/guest_vm.h>
#include <sel4vm/boot.h>

int
vm_init(vm_t *vm, vka_t *vka, simple_t *host_simple, allocman_t *allocman, vspace_t host_vspace, vm_plat_callbacks_t callbacks, int priority,
        ps_io_ops_t* io_ops, const char* name, void *cookie) {
    int err;
    bzero(vm, sizeof(vm_t));
    /* Initialise vm fields */
    vm->vka = vka;
    vm->simple = host_simple;
    vm->allocman = allocman;
    vm->io_ops = io_ops;
    vm->mem.vmm_vspace = host_vspace;
    vm->callbacks = callbacks;
    vm->tcb.priority = priority;
    vm->vm_name = strndup(name, strlen(name));
    /* Initialise ram region */
    vm->mem.num_ram_regions = 0;
    vm->mem.ram_regions = malloc(0);
    /* Currently set this to 4k pages by default */
    vm->mem.page_size = seL4_PageBits;
    /* Initialise our vcpu set */
    vm->vcpus = malloc(sizeof(vm_vcpu_t *) * MAX_NUM_VCPUS);
    assert(vm->vcpus);
    memset(vm->vcpus, 0, sizeof(vm_vcpu_t *) * MAX_NUM_VCPUS);

    /* Initialise vm architecture support */
    err = vm_init_arch(vm, cookie);
    if (err) {
        ZF_LOGE("Failed to initialise VM architecture support");
        return err;
    }

    /* Flag that the vm has been initialised */
    vm->vm_initialised = true;
    return 0;
}

int
vm_create_vcpu(vm_t *vm, void *cookie, vm_vcpu_t **vcpu, unsigned int vcpu_id) {
    int err;
    if(vcpu_id >= MAX_NUM_VCPUS) {
        ZF_LOGE("Invalid vcpu_id: id %d out of range", vcpu_id);
        return -1;
    }
    if(vm->vcpus[vcpu_id]) {
        ZF_LOGE("Invalid vcpu_id: id %d already initialised", vcpu_id);
        return -1;
    }
    vm_vcpu_t *vcpu_new = malloc(sizeof(vm_vcpu_t));
    assert(vcpu_new);
    bzero(vcpu_new, sizeof(vm_vcpu_t));
    /* Create VCPU */
    err = vka_alloc_vcpu(vm->vka, &vcpu_new->vm_vcpu);
    assert(!err);
    /* Initialise vcpu fields */
    vcpu_new->vm = vm;
    vcpu_new->vcpu_id = vcpu_id;
    err = vm_create_vcpu_arch(vm, cookie, vcpu_new);
    assert(!err);
    vm->vcpus[vcpu_id] = vcpu_new;
    *vcpu = vcpu_new;
    vm->num_vcpus++;
    return err;
}
