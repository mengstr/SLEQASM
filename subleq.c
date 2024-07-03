#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <errno.h>

#define MAX_CODE_SIZE (0xFFFFFF + 1 )
int32_t code[MAX_CODE_SIZE];
volatile bool die;
volatile bool ctrlc;

#define RING_BUFFER_SIZE 100000

typedef struct {
    volatile uint8_t buffer[RING_BUFFER_SIZE];
    volatile int head;
    volatile int tail;
} RingBuffer;

RingBuffer keypressBuffer;

// Function to initialize the ring buffer
void ring_buffer_init(RingBuffer *rb) {
    rb->head = rb->tail = 0;
}


// Function to pop a character from the ring buffer, returns -1 if buffer is empty
int ring_buffer_pop(RingBuffer *rb) {
    if (rb->head == rb->tail) {
        return -1; // Buffer is empty
    } else {
        int nextChar = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
        return nextChar;
    }
}

struct termios orig_termios;

// Function to reset the terminal to its original state
void reset_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// Function to set the terminal to raw mode
void set_raw_mode() {
    struct termios raw;

    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal_mode);

    raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Signal handler for SIGIO
void handle_keypress(int sig) {
    static int cnt=0;
    char c;
    while (read(STDIN_FILENO, &c, 1) > 0) {
        if (die) return;

        // Check for CTRL-C and set the flag if detected. The flag is read by the 
        // subleq program to check if CTRL-C was pressed. 
        // If pressed 5 times in a row, the subleq interpreter exits to shell/monitor
        if (c==3) {
            ctrlc=1;
            keypressBuffer.head=0;
            keypressBuffer.tail=0;
            cnt++;
            if (cnt>5) die=true; 
        } else cnt=0;
        
        int next = (keypressBuffer.head + 1) % RING_BUFFER_SIZE;
        keypressBuffer.buffer[keypressBuffer.head] = c;
        keypressBuffer.head = next;
    }
    // If read returns -1 and errno is set to EAGAIN or EWOULDBLOCK, the read would block, indicating no more data
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No more data to read
    }
}


// Set up asynchronous keypress handling
void setup_async_keypress() {
    struct sigaction sa;

    ring_buffer_init(&keypressBuffer);

    // Set up the signal handler for SIGIO
    sa.sa_handler = handle_keypress;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGIO, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Make stdin non-blocking and set it to generate SIGIO on input
    fcntl(STDIN_FILENO, F_SETFL, O_ASYNC | O_NONBLOCK);
    fcntl(STDIN_FILENO, F_SETOWN, getpid());
}


int main(int argc, char *argv[]) {
    FILE *file;
    char* filename;
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    filename = argv[1];
    uint32_t i = 0;

    file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open the file.\n");
        return 1;
    }

    // Skip the first line in the file
    char line[100];
    if (fgets(line, sizeof(line), file) == NULL) {
        printf("Failed to read the first line.\n");
        return 1;
    }

    while (fscanf(file, "%x", &code[i]) != EOF) {
        i++;
        if (i >= MAX_CODE_SIZE) {
            printf("Exceeded maximum code size.\n");
            break;
        }
    }

    fclose(file);

    set_raw_mode();    // Set terminal to raw mode
    setup_async_keypress();    // Set up asynchronous keypress handling


//    printf("Loaded %d instructions.\n", i);

    int pc = 0;
    int32_t a, b, c;
    ctrlc=0;
    while (!die) {
        a = code[pc];
        b = code[pc + 1];
        c = code[pc + 2];

        if (b==0xffffff) {      // -1 Write to console
            write(STDOUT_FILENO, &code[a], 1); // Write the character to stdou  t
            fflush(stdout); // Ensure all buffered output is written to stdout            
            pc += 3;
            continue;
        }

        if (a==0xffffff) {      // -1 Read from keyboard
            ctrlc = 0;
            int ch = ring_buffer_pop(&keypressBuffer);
            if (ch==-1) ch=0xffffff;
            code[b] = ch;
            pc += 3;
            continue;
        }

        if (a==0xfffffe) {      // -2 CTRL-C Check
            code[b] = ctrlc;
            ctrlc = 0;
            pc += 3;
            continue;
        }


        // Perform subtraction and ensure result wraps within 24-bit signed integer range
        int32_t result = (code[b] - code[a]) & 0xFFFFFF;
        if (result & 0x800000) result = -(0x1000000 - result);
        code[b] = result;

        if (code[b] <= 0) {
        if (c==0xffffff) { printf("\r\nHALT\r\n"); break;}
            pc = c;
        } else {
            pc += 3;
        }
    }
    
    reset_terminal_mode();
    printf("\r\nExiting the subleq interpreter.\r\n");
    return 0;
}





