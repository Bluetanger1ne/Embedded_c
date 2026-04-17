#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/util/queue.h"

#define LORA_UART uart1        // UART wired to LoRa
#define LORA_UART_TX 4          // GP4
#define LORA_UART_RX 5          // GP5
#define LORA_BAUD 9600          // LoRa default baud rate
#define SW0_PIN 9               // SW_0
#define UART_TIMEOUT_US 500000 // timeout 500ms ( 500 000 micro seconds)
// buffer definitions
#define MAX_BUF 128
#define MAX_DEV_BUF 20
#define MAX_ATTEMPTS 5

void wait_for_button(uint button_pin);
void process_deveui(const char *response, char *out);
void send_command(uart_inst_t *uart, const char *cmd);
bool response_ok(const char *response, const char *wanted_response);
bool read_uart_line(uart_inst_t *uart, char *buf);

int main(void) {
    stdio_init_all();

    // Initialize Lora uart
    uart_init(LORA_UART, LORA_BAUD);
    gpio_set_function(LORA_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(LORA_UART_RX, GPIO_FUNC_UART);

    // Initialize button
    gpio_init(SW0_PIN);
    gpio_set_dir(SW0_PIN, GPIO_IN);
    gpio_pull_up(SW0_PIN);

    // State machine variables
    int state = 1;
    int attempts = 0;
    char buf[MAX_BUF];
    char deveui[MAX_DEV_BUF] = {'\0'};

    while (true) {
        switch (state) {
            case 1:
                wait_for_button(SW0_PIN);
                attempts = 0;
                state = 2;
                break;

            case 2:
                send_command(LORA_UART, "AT");

                if (read_uart_line(LORA_UART, buf) && response_ok(buf, "+AT: OK")) {
                    printf("Connected to LoRa module\n");
                    state = 3;
                } else {
                    attempts++; // If the response is not received or the response is wrong more than 5 times, try again
                    if (attempts >= MAX_ATTEMPTS) {
                        printf("module not responding\n");
                        state = 1;
                    }
                }
                break;

            case 3:
                send_command(LORA_UART, "AT+VER");

                if (read_uart_line(LORA_UART, buf) && response_ok(buf, "+VER:")) {
                    printf("%s\n", buf);
                    state = 4;
                } else {
                    printf("Module stopped responding\n");
                    state = 1;
                }
                break;

            case 4:
                send_command(LORA_UART, "AT+ID=DevEui");

                if (read_uart_line(LORA_UART, buf) && response_ok(buf, "+ID: DevEui,")) {
                    process_deveui(buf, deveui); // Turn the devui from the original format to the lowercase version without colons.
                    printf("%s\n\n", deveui);
                    state = 1;
                } else {
                    printf("Module stopped responding\n");
                    state = 1;
                }
                break;
            default: ;
        }
    }
}

void wait_for_button(uint button_pin) {
    printf("Press SW_0 to start.\n\n");
    while (gpio_get(button_pin)) { // debounce and prevent pressing and holding the button
        tight_loop_contents();
    }
    sleep_ms(30);
    while (!gpio_get(button_pin)) {
        tight_loop_contents();
    }
}

bool read_uart_line(uart_inst_t *uart, char *buf) {
    int i = 0;

    while (uart_is_readable_within_us(uart, UART_TIMEOUT_US)) {
        char c = uart_getc(uart); // Get a character from uart
        if (c == '\n' || c == '\r') { // If the character read is \n or \r, and turn it into a \0
            if (i > 0) {
                buf[i] = '\0';
                return true;
            }
        }
        else if (i < MAX_BUF - 1) {
            buf[i] = c; // put the character into the buffer at a specific index
            i++;
        }
    }
    buf[i] = '\0'; // if not found, set first character to null terminator and return false
    return false;
}

void process_deveui(const char *response, char *out) {
    const char *start = strstr(response, "DevEui, ") + 8; // Start reading the deveui at the right place
    if (start == NULL) { // If it's empty return early
        out[0] = '\0';
        return;
    }
    int i = 0;

    while (*start != '\0') { // Check if the character being checked is \0 (Maybe redundant: *start != '\r' && *start != '\n' && *start != '\0')
        if (*start != ':') { // Skip the colon symbol and change all the letters to lowercase and put them into the processed string.
            out[i++] = tolower((unsigned char)*start);
        }
        start++;
    }
    out[i] = '\0';
}

void send_command(uart_inst_t *uart, const char *cmd) {
    uart_puts(uart, cmd);
    uart_puts(uart, "\r\n");
}

bool response_ok(const char *response, const char *wanted_response) {
    return strstr(response, wanted_response); // Check that the response contains an essential part of the expected response and return true if it does.
}
