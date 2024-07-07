# Modifed PureThermal 2 Firmware

Enables sending images over UART.

Images sent from PureThermal carrier board as available at 256000 baud in Y16 (14bit) greyscale format 120x160 resolution.

See original readme inside firmware folder for usage instructions.

Orginal repository:
https://github.com/groupgets/purethermal1-firmware
Release: 1.3.0

## Major Modifications

**/inc/project_config.h**
Enabled #define THERMAL_DATA_UART
Modifed #define USART_DEBUG_SPEED (256000)


**/src/lepton_task.h**
Removed enable_telemetry(); line ~113


**/src/uart_task.h**
Modified to send a single 120*160*2 array as the entire image after all four segments arrive. 

