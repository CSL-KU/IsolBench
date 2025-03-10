#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>


volatile int64_t count = 0;

// Signal handler function
void handle_sigint(int sig) {
  printf("\nReceived SIGINT (Ctrl+C). Exiting gracefully...\n");
  printf("count=%ld\n", count);
  exit(0);
}

int main()
{
  struct sigaction sa;

  sa.sa_handler = handle_sigint; // Set handler function
  sigemptyset(&sa.sa_mask);      // No additional signals blocked
  sa.sa_flags = 0;               // No special flags

  // Set the signal handler for SIGINT
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("Press Ctrl+C to terminate the program.\n");
    
  while(1) {
    count++;
  }

  // printf("count=%ld\n", count);
  return 0;
}
