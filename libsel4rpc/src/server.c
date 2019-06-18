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

#include <sel4nanopb/sel4nanopb.h>
#include <sel4rpc/server.h>
#include <sel4utils/api.h>
#include <simple/simple.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <rpc.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>

#include <utils/zf_log.h>

static int sel4rpc_handle_memory(sel4rpc_server_env_t *env, RpcMessage *rpcMsg)
{
    cspacepath_t path;
    int error;
    error = vka_cspace_alloc_path(env->vka, &path);
    if (error) {
        ZF_LOGE("Failed to alloc path: %d\n", error);
        sel4rpc_server_reply(env, 0, 1);
        return -1;
    }

    uintptr_t cookie;
    error = vka_utspace_alloc_at(env->vka, &path, rpcMsg->msg.memory.type, rpcMsg->msg.memory.size_bits,
                                 rpcMsg->msg.memory.address, &cookie);
    if (error) {
        ZF_LOGE("Failed to alloc at: %d\n", error);
        vka_cspace_free_path(env->vka, path);
        sel4rpc_server_reply(env, 0, 1);
        return -1;
    }

    seL4_SetCap(0, path.capPtr);
    int ret = sel4rpc_server_reply(env, 1, 0);

    vka_cspace_free_path(env->vka, path);
    return ret;
}

static int sel4rpc_handle_ioport(sel4rpc_server_env_t *env, RpcMessage *rpcMsg)
{
    cspacepath_t path;
    int error;
    error = vka_cspace_alloc_path(env->vka, &path);
    if (error) {
        ZF_LOGE("Failed to alloc path: %d\n", error);
        sel4rpc_server_reply(env, 0, 1);
        return -1;
    }

    seL4_Error err = simple_get_IOPort_cap(env->simple, rpcMsg->msg.ioport.start, rpcMsg->msg.ioport.end,
                                           path.root, path.capPtr, path.capDepth);
    if (err != seL4_NoError) {
        vka_cspace_free_path(env->vka, path);
        sel4rpc_server_reply(env, 0, 1);
        return err;
    }

    seL4_SetCap(0, path.capPtr);
    int ret = sel4rpc_server_reply(env, 1, 0);

    vka_cspace_free_path(env->vka, path);
    return ret;
}

static int sel4rpc_handle_irq(sel4rpc_server_env_t *env, RpcMessage *rpcMsg)
{
    cspacepath_t path;
    int error;
    error = vka_cspace_alloc_path(env->vka, &path);
    if (error) {
        ZF_LOGE("Failed to alloc path: %d\n", error);
        sel4rpc_server_reply(env, 0, 1);
        return -1;
    }

    seL4_Error err = seL4_InvalidArgument;
    switch (rpcMsg->msg.irq.which_type) {
    case IrqAllocMessage_msi_tag: {
        /* x86 MSI IRQ */
#ifdef CONFIG_ARCH_X86
        IrqAllocMessagex86_MSI *msi = &rpcMsg->msg.irq.type.msi;
        err = arch_simple_get_msi(&env->simple->arch_simple, path, msi->pci_bus,
                                  msi->pci_dev, msi->pci_func, msi->handle, msi->vector);
#endif
        break;
    }
    case IrqAllocMessage_ioapic_tag: {
        /* x86 IOAPIC IRQ */
#ifdef CONFIG_ARCH_X86
        IrqAllocMessagex86_IOAPIC *ioapic = &rpcMsg->msg.irq.type.ioapic;
        err = arch_simple_get_ioapic(&env->simple->arch_simple, path, ioapic->ioapic,
                                     ioapic->pin, ioapic->level, ioapic->polarity, ioapic->vector);
#endif
        break;
    }
    case IrqAllocMessage_simple_tag: {
        /* Simple IRQ */
        IrqAllocMessageSimple *simple = &rpcMsg->msg.irq.type.simple;
#ifdef CONFIG_ARCH_simple
        if (simple->setTrigger) {
            err = arch_simple_get_IRQ_trigger(&env->simple->arch_simple, simple->irq,
                                              simple->trigger, path);
        } else
#endif
        {
            err = simple_get_IRQ_handler(env->simple, simple->irq, path);
        }
        break;
    }
    default:
        ZF_LOGE("Unknown IRQ type");
        vka_cspace_free_path(env->vka, path);
        return -1;
    }

    if (err != seL4_NoError) {
        vka_cspace_free_path(env->vka, path);
        sel4rpc_server_reply(env, 0, 1);
        return err;
    }

    seL4_SetCap(0, path.capPtr);
    int ret = sel4rpc_server_reply(env, 1, 0);

    vka_cspace_free_path(env->vka, path);
    return ret;
}

int sel4rpc_default_handler(sel4rpc_server_env_t *env, UNUSED void *data, RpcMessage *rpcMsg)
{
    cspacepath_t path;
    path.capPtr = seL4_CapNull;
    switch (rpcMsg->which_msg) {
    case RpcMessage_memory_tag:
        return sel4rpc_handle_memory(env, rpcMsg);
    case RpcMessage_ioport_tag:
        return sel4rpc_handle_ioport(env, rpcMsg);
    case RpcMessage_irq_tag:
        return sel4rpc_handle_irq(env, rpcMsg);
    default:
        ZF_LOGE("Not sure what to do!");
    }

    return -1;
}

int sel4rpc_server_init(sel4rpc_server_env_t *env, vka_t *vka,
                        sel4rpc_handler_t handler_func, void *data, vka_object_t *reply, simple_t *simple)
{
    env->vka = vka;
    env->reply = reply;
    env->handler = handler_func;
    env->data = data;
    env->simple = simple;
    return 0;
}

int sel4rpc_server_reply(sel4rpc_server_env_t *env, int caps, int errorCode)
{
    pb_ostream_t ostream = pb_ostream_from_IPC(0);
    RpcMessage rpcMsg;
    rpcMsg.which_msg = RpcMessage_errorCode_tag;
    rpcMsg.msg.errorCode = errorCode;

    bool ret = pb_encode_delimited(&ostream, &RpcMessage_msg, &rpcMsg);
    if (!ret) {
        /* encode failed, clean up any caps */
        while (caps > 0) {
            caps--;
            vka_cspace_free(env->vka, seL4_GetCap(caps));
        }
        ZF_LOGE("Failed to encode reply (%s)", PB_GET_ERROR(&ostream));
        return -1;
    }

    size_t size = ostream.bytes_written / sizeof(seL4_Word);
    if (ostream.bytes_written % sizeof(seL4_Word)) {
        size++;
    }

    api_reply(env->reply->cptr, seL4_MessageInfo_new(0, 0, caps, size));

    return 0;
}

int sel4rpc_server_recv(sel4rpc_server_env_t *env)
{
    RpcMessage rpcMsg;
    pb_istream_t stream = pb_istream_from_IPC(1);
    bool ret = pb_decode_delimited(&stream, &RpcMessage_msg, &rpcMsg);
    if (!ret) {
        ZF_LOGE("Invalid protobuf stream (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    int err = 0;
    if (env->handler) {
        err = env->handler(env, env->data, &rpcMsg);
    } else {
        err = sel4rpc_default_handler(env, NULL, &rpcMsg);
    }
    return err;
}