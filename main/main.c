#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/rtc.h"

#define TRIGGER_PIN 14  
#define ECHO_PIN    15

volatile uint64_t start_time = 0;
volatile uint64_t end_time   = 0;
volatile bool measuring      = false;
volatile int failure_alarm_id = -1;

int64_t timer_failure_callback(alarm_id_t id, void *user_data) {
    if (measuring) {
        measuring = false;
    }
    failure_alarm_id = -1;
    start_time = 0;
    end_time = 0;
    return 0; 
}

void echo_callback(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_RISE) {
        start_time = time_us_64();
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        end_time = time_us_64();
        measuring = false;
        if (failure_alarm_id >= 0) {
            cancel_alarm(failure_alarm_id);
            failure_alarm_id = -1;
        }
    }
}

float measure_distance() {
    start_time = 0;
    end_time = 0;

    gpio_put(TRIGGER_PIN, 1);
    sleep_us(10);
    gpio_put(TRIGGER_PIN, 0);

    measuring = true;
    failure_alarm_id = add_alarm_in_us(30000, timer_failure_callback, NULL, false);

    while (measuring) {
        tight_loop_contents();
    }

    if (end_time > start_time) {
        uint64_t pulse_width = end_time - start_time;
        float distance = (pulse_width * 0.0343f) / 2.0f;
        return distance;
    } else {
        return -1.0f; 
    }
}

void get_time_str(char *buffer) {
    datetime_t t;
    rtc_get_datetime(&t);
    sprintf(buffer, "%02d:%02d:%02d", t.hour, t.min, t.sec);
}

int main() {
    stdio_init_all();
    sleep_ms(2000);  

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_pulls(ECHO_PIN, false, true);

    gpio_set_irq_enabled_with_callback(
        ECHO_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &echo_callback
    );

    datetime_t t = {
        .year  = 2024,
        .month = 3,
        .day   = 17,
        .dotw  = 0,  // Domingo
        .hour  = 22,
        .min   = 10,
        .sec   = 0
    };
    rtc_init();
    rtc_set_datetime(&t);

    bool sensor_running = false;
    int consecutive_failures = 0;
    bool alarm_triggered = false;

    printf("Digite 's' para iniciar e 'p' para parar:\n");

    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == 's') {
                sensor_running = true;
                consecutive_failures = 0;
                alarm_triggered = false;
                printf("Medições iniciadas.\n");
            } else if (ch == 'p') {
                sensor_running = false;
                printf("Medições paradas.\n");
            }
        }

        if (sensor_running) {
            char time_str[10];
            get_time_str(time_str);

            float distance = measure_distance();
            if (distance > 0) {
                consecutive_failures = 0; 
                if (alarm_triggered) {
                    printf("Sensor reconectado, medições retomadas.\n");
                    alarm_triggered = false;
                }
                printf("%s - Distância: %.1f cm\n", time_str, distance);
            } else {
                consecutive_failures++;
                printf("%s - Falha na leitura do sensor\n", time_str);

                if (consecutive_failures >= 3 && !alarm_triggered) {
                    printf("ALARME: Sensor desconectado ou inoperante!\n");
                    alarm_triggered = true;
                }
            }
        }
        sleep_ms(1000);
    }

    return 0;
}
