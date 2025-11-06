# Office Bakery: Multi-threaded Synchronization
> cake

This repo is the demon of a multi-threaded sync problem simulating an office bakery with limited resources. We have mutex locks, condition variables, and semaphores to coordinate customer flow and chef operations while enforcing strict capacity and ordering constraints.

Problem constraints
-------------------
**Resources:**
- 4 ovens (max 4 concurrent cake baking operations)
- 4 chefs (handle baking and payment)
- 4 sofa seats (waiting area for customers being served)
- 1 cash register (serialized payment processing)
- 25 total customer capacity

**Customer workflow:**
1. `enterOfficeBakery()` — enter if capacity < 25
2. `sitOnSofa()` — take seat if available (max 4), otherwise stand and wait
3. `getCake()` — paired with chef's `bakeCake()`, up to 4 concurrent operations
4. `pay()` — wait for chef to accept payment
5. Exit and free sofa seat

**Chef workflow:**
1. `acceptPayment()` — prioritized over baking, uses shared cash register (mutex-protected)
2. `bakeCake()` — paired with customer's `getCake()`, uses oven resource
3. Learn recipes (idle state when no customers)

**Timing:**
- Customer actions: 1 second each (enter, sit, getCake, pay)
- Chef actions: 2 seconds each (bakeCake, acceptPayment)
- Actions are atomic and cannot be interrupted

**Synchronization rules:**
- No entry if bakery at capacity (25 customers)
- No sitting if sofa full (4 seats)
- FIFO ordering for sofa seating (longest-standing customer gets next available seat)
- FIFO ordering for service (longest-seated customer gets next available chef)
- getCake and bakeCake must execute concurrently (rendezvous)
- Payment serialized (one customer at a time via cash register)
- Customer cannot exit until chef completes acceptPayment
- Chefs prioritize payment over baking

Build
-----
```bash
make
```

Uses pthreads for threading, semaphores/mutexes for synchronization. Link with `-pthread`.
Protocol and behavior summary
------------------------------
**Synchronization primitives used:**
- **Mutex locks**: protect shared counters (customers in bakery, sofa occupancy, capacity checks)
- **Condition variables**: signal sofa availability, chef availability, payment completion
- **Semaphores**: enforce resource limits (4 ovens, 1 cash register), implement FIFO queues

> here is a list of my favorite cake flavours: chocolate, black forest, blueberry, cheescake, pineapple

> here are some cakes i think are overrated: strawberry, mango, butterscotch, white forest
