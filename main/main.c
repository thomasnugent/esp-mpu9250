/*****************************************************************************
 *                                                                           *
 *  Copyright 2018 Simon M. Werner                                           *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *      http://www.apache.org/licenses/LICENSE-2.0                           *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_task_wdt.h"

#include "driver/i2c.h"
#include "driver/uart.h"

#include "../components/ahrs/MadgwickAHRS.h"
#include "../components/mpu9250/mpu9250.h"
#include "../components/mpu9250/calibrate.h"
#include "../components/mpu9250/common.h"

static const char *TAG = "main";

#define I2C_MASTER_NUM I2C_NUM_0 /*!< I2C port number for master dev */

// Default cal!
calibration_t cal = {
    .mag_offset = {.x = 0.0, .y = 0.0, .z = 0.0},
    .mag_scale = {.x = 1.0, .y = 1.0, .z = 1.0},
    .accel_offset = {.x = 0.0, .y = 0.0, .z = 0.0},
    .accel_scale_lo = {.x = -1.0, .y = -1.0, .z = -1.0},
    .accel_scale_hi = {.x = 1.0, .y = 1.0, .z = 1.0},
    .gyro_bias_offset = {.x = 0.0, .y = 0.0, .z = 0.0}};

/**
 * Transformation:
 *  - Rotate around Z axis 180 degrees
 *  - Rotate around X axis -90 degrees
 * @param  {object} s {x,y,z} sensor
 * @return {object}   {x,y,z} transformed
 */
static void transform_accel_gyro(vector_t *v)
{
  float x = v->x;
  float y = v->y;
  float z = v->z;

  v->x = -x;
  v->y = -z;
  v->z = -y;
}

/**
 * Transformation: to get magnetometer aligned
 * @param  {object} s {x,y,z} sensor
 * @return {object}   {x,y,z} transformed
 */
static void transform_mag(vector_t *v)
{
  float x = v->x;
  float y = v->y;
  float z = v->z;

  v->x = -y;
  v->y = z;
  v->z = -x;
}

void run_imu(void)
{
  ESP_LOGI(TAG, "HELLO");
  i2c_mpu9250_init(&cal);
  MadgwickAHRSinit(SAMPLE_FREQ_Hz, 0.8);

  uint64_t i = 0;
  while (true)
  {
    vector_t va, vg, vm;

    // Get the Accelerometer, Gyroscope and Magnetometer values.
    ESP_ERROR_CHECK(get_accel_gyro_mag(&va, &vg, &vm));

    // Transform these values to the orientation of our device.
    transform_accel_gyro(&va);
    transform_accel_gyro(&vg);
    transform_mag(&vm);

    // // Apply the AHRS algorithm
    // MadgwickAHRSupdate(DEG2RAD(vg.x), DEG2RAD(vg.y), DEG2RAD(vg.z),
    //                    va.x, va.y, va.z,
    //                    vm.x, vm.y, vm.z);

    // Print the data out every 10 items
    if (i++ % 10 == 0)
    {
      float temp;
      ESP_ERROR_CHECK(get_temperature_celsius(&temp));

      // float heading, pitch, roll;
      // MadgwickGetEulerAnglesDegrees(&heading, &pitch, &roll);
      printf("gx: %9.2f gy: %9.2f gz: %9.2f "
             "ax: %9.2f ay: %9.2f az: %9.2f "
             "mx: %9.2f my: %9.2f mz: %9.2f temp: %9.2fC\n",
             vg.x, vg.y, vg.z,
             va.x, va.y, va.z,
             vm.x, vm.y, vm.z, temp);
      // printf("FSR X X X X X X X X heading: %2.3f pitch: %2.3f roll: %2.3f Temp %2.3fC h h h h\r\n", heading, pitch, roll, temp); 
      // printf("FSR X X X X X X X X heading: %2.3f pitch: %2.3f roll: %2.3f Temp %2.3fC h h h h\r\n", heading, pitch, roll, temp);
      // ESP_LOGI(TAG, "FSR X X X X X X X X heading: %2.3f pitch: %2.3f roll: %2.3f Temp %2.3fC h h h h", heading, pitch, roll, temp);

      // Make the WDT happy
      esp_task_wdt_reset();
    }

    pause();
  }
}

static void imu_task(void *arg)
{

#ifdef CONFIG_CALIBRATION_MODE
  calibrate_gyro();
  calibrate_accel();
  calibrate_mag();
#else
  run_imu();
#endif

  // Exit
  vTaskDelay(100 / portTICK_RATE_MS);
  i2c_driver_delete(I2C_MASTER_NUM);

  vTaskDelete(NULL);
}

void app_main(void)
{
  uart_set_baudrate(UART_NUM_0, 115200);

  //start i2c task
  xTaskCreate(imu_task, "imu_task", 2048, NULL, 10, NULL);
}