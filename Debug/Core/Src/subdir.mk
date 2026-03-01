################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_metadata.c \
../Core/Src/at_command.c \
../Core/Src/bootloader_download.c \
../Core/Src/bootloader_logic.c \
../Core/Src/flash_ops.c \
../Core/Src/main.c \
../Core/Src/sha256.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c 

OBJS += \
./Core/Src/app_metadata.o \
./Core/Src/at_command.o \
./Core/Src/bootloader_download.o \
./Core/Src/bootloader_logic.o \
./Core/Src/flash_ops.o \
./Core/Src/main.o \
./Core/Src/sha256.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o 

C_DEPS += \
./Core/Src/app_metadata.d \
./Core/Src/at_command.d \
./Core/Src/bootloader_download.d \
./Core/Src/bootloader_logic.d \
./Core/Src/flash_ops.d \
./Core/Src/main.d \
./Core/Src/sha256.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_metadata.cyclo ./Core/Src/app_metadata.d ./Core/Src/app_metadata.o ./Core/Src/app_metadata.su ./Core/Src/at_command.cyclo ./Core/Src/at_command.d ./Core/Src/at_command.o ./Core/Src/at_command.su ./Core/Src/bootloader_download.cyclo ./Core/Src/bootloader_download.d ./Core/Src/bootloader_download.o ./Core/Src/bootloader_download.su ./Core/Src/bootloader_logic.cyclo ./Core/Src/bootloader_logic.d ./Core/Src/bootloader_logic.o ./Core/Src/bootloader_logic.su ./Core/Src/flash_ops.cyclo ./Core/Src/flash_ops.d ./Core/Src/flash_ops.o ./Core/Src/flash_ops.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/sha256.cyclo ./Core/Src/sha256.d ./Core/Src/sha256.o ./Core/Src/sha256.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su

.PHONY: clean-Core-2f-Src

