#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"


struct sock {
  struct sock *next;
  uint32 raddr;
  uint16 lport;
  uint16 rport;
  struct spinlock lock;
  struct mbufq rxq;
};

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void) {
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport) {
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr && pos->lport == lport && pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si) kfree((char*)si);
  if (*f) fileclose(*f);
  return -1;
}

int
sockread(struct file *f, uint64 addr, int n) {
  struct sock *s = f->sock;

  acquire(&s->lock);
  while (mbufq_empty(&s->rxq)) {
    sleep(&s->rxq, &s->lock);
  }
  struct mbuf *m = mbufq_pophead(&s->rxq);
  release(&s->lock);

  int len = m->len < n ? m->len : n;
  if (copyout(myproc()->pagetable, addr, m->head, len) < 0) {
    mbuffree(m);
    return -1;
  }
  mbuffree(m);
  return len;
}

int
sockwrite(struct file *f, uint64 addr, int n) {
  struct sock *s = f->sock;
  struct mbuf *m;

  // ✅ 为以太网/IP/UDP 头预留足够空间
  if ((m = mbufalloc(MBUF_DEFAULT_HEADROOM)) == 0)
    return -1;

  // ✅ 添加数据区域
  if (mbufput(m, n) == 0) {
    mbuffree(m);
    return -1;
  }

  // ✅ 拷贝用户数据
  if (copyin(myproc()->pagetable, m->head, addr, n) < 0) {
    mbuffree(m);
    return -1;
  }

  // ✅ 发送 UDP 包
  net_tx_udp(m, s->raddr, s->lport, s->rport);

  return n;
}


int
sockclose(struct file *f) {
  struct sock *s = f->sock;

  acquire(&lock);
  if (sockets == s) {
    sockets = s->next;
  } else {
    struct sock *prev = sockets;
    while (prev && prev->next != s)
      prev = prev->next;
    if (prev)
      prev->next = s->next;
  }
  release(&lock);

  acquire(&s->lock);
  while (!mbufq_empty(&s->rxq)) {
    struct mbuf *m = mbufq_pophead(&s->rxq);
    mbuffree(m);
  }
  release(&s->lock);
  kfree((char *)s);
  return 0;
}

void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport) {
  acquire(&lock);
  struct sock *s = sockets;
  while (s) {
    if (s->lport == lport && s->raddr == raddr && s->rport == rport) {
      acquire(&s->lock);
      mbufq_pushtail(&s->rxq, m);   // 用队列的入队函数
      wakeup(&s->rxq);
      release(&s->lock);
      release(&lock);
      return;
    }
    s = s->next;
  }
  release(&lock);

  mbuffree(m);
}


