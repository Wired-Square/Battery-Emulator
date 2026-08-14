#ifndef EMUL_DRIVER_UART_H
#define EMUL_DRIVER_UART_H

typedef int uart_port_t;
typedef int esp_err_t;

#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)
#define ESP_OK 0
#define UART_MODE_RS485_HALF_DUPLEX 3

inline esp_err_t uart_set_pin(uart_port_t, int, int, int, int) {
  return ESP_OK;
}
inline esp_err_t uart_set_mode(uart_port_t, int) {
  return ESP_OK;
}

#endif
