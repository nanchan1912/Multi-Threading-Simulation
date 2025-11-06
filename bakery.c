#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>

// Data structures
struct Customer {
    int id;
    int enter_time;
    pthread_cond_t sit_cond;
    pthread_cond_t request_cond;
    pthread_cond_t bake_done_cond;
    pthread_cond_t payment_done_cond;
    struct Chef *serving_chef;
};

struct Chef {
    int id;
    pthread_cond_t cond;
};

struct Node {
    struct Customer *cust;
    struct Node *next;
};

// Global variables
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_work = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_seat_available = PTHREAD_COND_INITIALIZER;
sem_t cash_sem;
int inside = 0;              // Customers in shop
int seated_count = 0;        // Customers on sofa
struct Node *seated_head = NULL;    // Sofa queue (FIFO)
struct Node *standing_head = NULL;  // Standing queue (sorted by enter_time)
struct Node *payment_head = NULL;   // Payment queue (FIFO)
time_t real_start;           // Real-world start time
int min_time = -1;           // Earliest input timestamp
int num_cust = 0;
int completed = 0;
int done = 0;
pthread_mutex_t completed_mutex = PTHREAD_MUTEX_INITIALIZER;

// Get current simulated time
int get_current_time() {
    return min_time + (int)difftime(time(NULL), real_start);
}

// Linked list operations
void push_back(struct Node **head, struct Customer *cust) {
    struct Node *new = malloc(sizeof(struct Node));
    new->cust = cust;
    new->next = NULL;
    if (*head == NULL) {
        *head = new;
    } else {
        struct Node *tmp = *head;
        while (tmp->next) tmp = tmp->next;
        tmp->next = new;
    }
}

struct Customer *pop_front(struct Node **head) {
    if (*head == NULL) return NULL;
    struct Node *tmp = *head;
    *head = tmp->next;
    struct Customer *c = tmp->cust;
    free(tmp);
    return c;
}

void insert_sorted(struct Node **head, struct Customer *cust) {
    struct Node *new = malloc(sizeof(struct Node));
    new->cust = cust;
    new->next = NULL;
    if (*head == NULL || (*head)->cust->enter_time >= cust->enter_time) {
        new->next = *head;
        *head = new;
    } else {
        struct Node *tmp = *head;
        while (tmp->next && tmp->next->cust->enter_time < cust->enter_time)
            tmp = tmp->next;
        new->next = tmp->next;
        tmp->next = new;
    }
}

// Customer thread
void *customer_thread(void *arg) {
    struct Customer *self = (struct Customer *)arg;

    // Enter bakery
    pthread_mutex_lock(&mutex);
    if (inside >= 25) {
        pthread_mutex_unlock(&mutex);
        
        // Increment completed for rejected customers
        pthread_mutex_lock(&completed_mutex);
        completed++;
        pthread_mutex_unlock(&completed_mutex);
        
        pthread_cond_destroy(&self->sit_cond);
        pthread_cond_destroy(&self->request_cond);
        pthread_cond_destroy(&self->bake_done_cond);
        pthread_cond_destroy(&self->payment_done_cond);
        free(self);
        return NULL;
    }
    inside++;
    int t = get_current_time();
    self->enter_time = t;
    printf("%d Customer %d enters\n", t, self->id);
    fflush(stdout);
    pthread_mutex_unlock(&mutex);
    sleep(1);

    // Try to sit
    pthread_mutex_lock(&mutex);
    while (seated_count >= 4) {
        insert_sorted(&standing_head, self);
        pthread_cond_wait(&self->sit_cond, &mutex);
    }
    seated_count++;
    push_back(&seated_head, self);
    printf("%d Customer %d sits\n", get_current_time(), self->id);
    fflush(stdout);
    pthread_cond_broadcast(&cond_work); // Signal chefs that a customer is seated
    pthread_mutex_unlock(&mutex);
    sleep(1);

    // Wait for chef to be assigned
    pthread_mutex_lock(&mutex);
    while (self->serving_chef == NULL) {
        pthread_cond_wait(&self->request_cond, &mutex);
    }
    printf("%d Customer %d requests cake\n", get_current_time(), self->id);
    fflush(stdout);
    pthread_mutex_unlock(&mutex);
    sleep(1);
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&self->serving_chef->cond); // Signal chef to bake after request action
    pthread_mutex_unlock(&mutex);

    // Wait for baking to complete
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&self->bake_done_cond, &mutex);
    pthread_mutex_unlock(&mutex);

    // Pay
    t = get_current_time();
    printf("%d Customer %d pays\n", t, self->id);
    fflush(stdout);
    sleep(1);

    // Queue for payment
    pthread_mutex_lock(&mutex);
    push_back(&payment_head, self);
    pthread_cond_broadcast(&cond_work); // Signal chefs for payment
    pthread_cond_wait(&self->payment_done_cond, &mutex);
    pthread_mutex_unlock(&mutex);

    // Leave - CRITICAL: Free the sofa seat BEFORE leaving
    pthread_mutex_lock(&mutex);
    seated_count--;
    // Wake up a standing customer to take the freed seat
    if (standing_head != NULL) {
        struct Customer *next = pop_front(&standing_head);
        pthread_cond_signal(&next->sit_cond);
    }
    pthread_mutex_unlock(&mutex);

    t = get_current_time();
    printf("%d Customer %d leaves\n", t, self->id);
    fflush(stdout);
    sleep(1);

    // Cleanup
    pthread_mutex_lock(&mutex);
    inside--;
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&completed_mutex);
    completed++;
    pthread_mutex_unlock(&completed_mutex);

    pthread_cond_destroy(&self->sit_cond);
    pthread_cond_destroy(&self->request_cond);
    pthread_cond_destroy(&self->bake_done_cond);
    pthread_cond_destroy(&self->payment_done_cond);
    free(self);
    return NULL;
}

// Chef thread
void *chef_thread(void *arg) {
    struct Chef *self = (struct Chef *)arg;
    while (1) {
        pthread_mutex_lock(&mutex);
        if (done) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        while (payment_head == NULL && seated_head == NULL && !done) {
            pthread_cond_wait(&cond_work, &mutex);
        }
        if (done) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        if (payment_head != NULL) {
            // Prioritize payment
            struct Customer *c = pop_front(&payment_head);
            pthread_mutex_unlock(&mutex);
            sem_wait(&cash_sem);
            int t = get_current_time();
            printf("%d Chef %d accepts payment for Customer %d\n", t, self->id, c->id);
            fflush(stdout);
            sleep(2);
            sem_post(&cash_sem);
            pthread_mutex_lock(&mutex);
            pthread_cond_signal(&c->payment_done_cond);
            pthread_mutex_unlock(&mutex);
        } else if (seated_head != NULL) {
            // Bake for customer
            struct Customer *c = pop_front(&seated_head);
            // DON'T decrement seated_count here - customer is still occupying the seat
            c->serving_chef = self;
            pthread_cond_signal(&c->request_cond);
            pthread_cond_wait(&self->cond, &mutex);
            // now woken, mutex locked
            pthread_mutex_unlock(&mutex);

            int t = get_current_time();
            printf("%d Chef %d bakes for Customer %d\n", t, self->id, c->id);
            fflush(stdout);
            sleep(2);

            pthread_mutex_lock(&mutex);
            pthread_cond_signal(&c->bake_done_cond);
            pthread_mutex_unlock(&mutex);
        } else {
            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}

// Main function
int main() {
    // Initialize semaphore for cash register
    sem_init(&cash_sem, 0, 1);

    printf("Welcome to the bakery! Enter your orders:\n");
    fflush(stdout);

    // Read input first to determine if there are any customers
    struct Arrival {
        int time;
        int id;
    };
    struct Arrival arrivals[100];
    char line[100];
    while (fgets(line, sizeof(line), stdin)) {
        if (strcmp(line, "<EOF>\n") == 0 || strcmp(line, "<EOF>") == 0) break;
        int t, id;
        if (sscanf(line, "%d Customer %d", &t, &id) == 2) {
            arrivals[num_cust].time = t;
            arrivals[num_cust].id = id;
            num_cust++;
            if (min_time == -1 || t < min_time) min_time = t;
        }
    }

    // If no customers, exit cleanly without additional output
    if (num_cust == 0) {
        sem_destroy(&cash_sem);
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond_work);
        pthread_cond_destroy(&cond_seat_available);
        pthread_mutex_destroy(&completed_mutex);
        return 0;
    }

    // Initialize chefs
    struct Chef chefs[4];
    pthread_t chef_threads[4];
    for (int i = 0; i < 4; i++) {
        chefs[i].id = i + 1;
        pthread_cond_init(&chefs[i].cond, NULL);
        pthread_create(&chef_threads[i], NULL, chef_thread, &chefs[i]);
    }

    // Sort arrivals by time
    for (int i = 0; i < num_cust; i++) {
        for (int j = i + 1; j < num_cust; j++) {
            if (arrivals[i].time > arrivals[j].time) {
                struct Arrival tmp = arrivals[i];
                arrivals[i] = arrivals[j];
                arrivals[j] = tmp;
            }
        }
    }

    // Process customers
    real_start = time(NULL);
    int current_index = 0;
    while (current_index < num_cust) {
        int next_time = arrivals[current_index].time;
        long delay = (next_time - min_time) - difftime(time(NULL), real_start);
        if (delay > 0) sleep(delay);

        struct Customer *cust = malloc(sizeof(struct Customer));
        cust->id = arrivals[current_index].id;
        cust->serving_chef = NULL;
        pthread_cond_init(&cust->sit_cond, NULL);
        pthread_cond_init(&cust->request_cond, NULL);
        pthread_cond_init(&cust->bake_done_cond, NULL);
        pthread_cond_init(&cust->payment_done_cond, NULL);

        pthread_t th;
        pthread_create(&th, NULL, customer_thread, cust);
        pthread_detach(th);
        current_index++;
    }

    // Wait for all customers to finish
    while (1) {
        pthread_mutex_lock(&completed_mutex);
        if (completed >= num_cust) {
            pthread_mutex_unlock(&completed_mutex);
            break;
        }
        pthread_mutex_unlock(&completed_mutex);
        sleep(1);
    }

    // Shut down chefs
    pthread_mutex_lock(&mutex);
    done = 1;
    pthread_cond_broadcast(&cond_work);
    pthread_mutex_unlock(&mutex);

    // Join chef threads
    for (int i = 0; i < 4; i++) {
        pthread_join(chef_threads[i], NULL);
        pthread_cond_destroy(&chefs[i].cond);
    }

    printf("All orders processed. Bakery is closing. Thank you!\n");
    fflush(stdout);

    // Cleanup
    sem_destroy(&cash_sem);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond_work);
    pthread_cond_destroy(&cond_seat_available);
    pthread_mutex_destroy(&completed_mutex);

    return 0; 
}
