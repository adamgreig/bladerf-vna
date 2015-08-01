#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <libbladeRF.h>

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"

struct stream_data {
    void            **buffers;
    size_t          num_buffers;
    size_t          samples_per_buffer;
    size_t          num_transfers;
    unsigned int    next_buffer;
    bladerf_module  module;
    int             samples_left;
};

struct thread_data {
    struct bladerf* dev;
    struct bladerf_stream* stream;
    struct stream_data* stream_data;
    volatile int* start;
    int  rv;
};

void* stream_cb(struct bladerf *dev, struct bladerf_stream *stream,
                struct bladerf_metadata *md, void* samples, size_t num_samples,
                void* user_data)
{
    void *rv;
    struct stream_data *data = user_data; 

    data->samples_left -= num_samples;
    if(data->samples_left <= 0) {
        return NULL;
    }

    rv = data->buffers[data->next_buffer];
    data->next_buffer = (data->next_buffer + 1) % data->num_buffers;
    return rv;
}

void* txrx_thread(void* arg)
{
    struct thread_data* my_thread_data = (struct thread_data*)arg;
    while(!*(my_thread_data->start));
    my_thread_data->rv = bladerf_stream(my_thread_data->stream,
                                        my_thread_data->stream_data->module);
    return NULL;
}

void ignore_sigint(int sig)
{
    return;
}

int main(int argc, char* argv[])
{
    int i, j, k, pos, result;

    struct bladerf* dev = NULL;
    struct bladerf_devinfo* devices = NULL;
    struct bladerf_stream* tx_stream;
    struct bladerf_stream* rx_stream;

    unsigned int f_low  = 250000000;
    unsigned int f_high = 700000000;
    unsigned int f_step =   1000000;
    unsigned int freq   = f_low;
    int tx_gain = 56;
    int rx_gain = 3;

    size_t num_buffers = 24;
    size_t samples_per_buffer = 8192;
    size_t num_transfers = 8;

    unsigned int sample_rate = 4000000;
    unsigned int bandwidth =   1500000;

    struct stream_data tx_stream_data;
    struct stream_data rx_stream_data;
    struct thread_data tx_thread_data;
    struct thread_data rx_thread_data;

    pthread_t tx_thread_pth;
    pthread_t rx_thread_pth;
    volatile int threads_start = 0;

    double rx_power;

    double* tx_freqs;
    double* rx_powers;

    int n_steps = 1 + (f_high - f_low) / f_step;

    tx_freqs = malloc(n_steps * sizeof(double));
    rx_powers = malloc(n_steps * sizeof(double));

    signal(SIGINT, ignore_sigint);
    
    printf("%-50s", "Searching for BladeRFs...");
    fflush(stdout);
    result = bladerf_get_device_list(&devices);
    if(result < 0) goto error;
    else printf(KGRN "OK" KNRM "\n");
    
    if(result == 0) {
        printf("No BladeRFs found, exiting.\n");
        return 1;
    } else for(i=0; i<result; i++) {
        printf("Found BladeRF[%d]:\n", i);
        printf("    Backend: %s\n", bladerf_backend_str(devices[i].backend));
        printf("    Serial: %s\n", devices[i].serial);
        printf("    USB: %X:%X\n", devices[i].usb_bus, devices[i].usb_addr);
        printf("    Instance ID: %d\n", devices[i].instance);
    }
    
    printf("%-50s", "Opening device 0...");
    fflush(stdout);
    result = bladerf_open_with_devinfo(&dev, &devices[0]);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("%-50s", "Disabling loopback...");
    fflush(stdout);
    result = bladerf_set_loopback(dev, BLADERF_LB_NONE);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("Setting frequency to %eHz...%n", (double)freq, &pos);
    printf("%*s", 50-pos, " ");
    fflush(stdout);
    result = bladerf_set_frequency(dev, BLADERF_MODULE_TX, freq);
    if(result < 0) goto error;
    result = bladerf_set_frequency(dev, BLADERF_MODULE_RX, freq);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("Setting sample rates to %uHz...%n", sample_rate, &pos);
    printf("%*s", 50-pos, " ");
    fflush(stdout);
    result = bladerf_set_sample_rate(dev, BLADERF_MODULE_TX, sample_rate, NULL);
    if(result < 0) goto error;
    result = bladerf_set_sample_rate(dev, BLADERF_MODULE_RX, sample_rate, NULL);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("Setting bandwidth to %uHz...%n", bandwidth, &pos);
    printf("%*s", 50-pos, " ");
    fflush(stdout);
    result = bladerf_set_bandwidth(dev, BLADERF_MODULE_TX, bandwidth, NULL);
    if(result < 0) goto error;
    result = bladerf_set_bandwidth(dev, BLADERF_MODULE_RX, bandwidth, NULL);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("Setting TX gain to %ddB...%n", tx_gain, &pos);
    printf("%*s", 50-pos, " ");
    fflush(stdout);
    result = bladerf_set_gain(dev, BLADERF_MODULE_TX, tx_gain);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("Setting RX gain to %ddB...%n", rx_gain, &pos);
    printf("%*s", 50-pos, " ");
    fflush(stdout);
    result = bladerf_set_gain(dev, BLADERF_MODULE_RX, rx_gain);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("%-50s", "Setting LPF mode to normal...");
    fflush(stdout);
    result = bladerf_set_lpf_mode(dev, BLADERF_MODULE_TX, BLADERF_LPF_NORMAL);
    if(result < 0) goto error;
    result = bladerf_set_lpf_mode(dev, BLADERF_MODULE_RX, BLADERF_LPF_NORMAL);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("%-50s", "Enabling TX and RX modules...");
    fflush(stdout);
    result = bladerf_enable_module(dev, BLADERF_MODULE_RX, true);
    if(result < 0) goto error;
    result = bladerf_enable_module(dev, BLADERF_MODULE_TX, true);
    if(result < 0) goto error;
    printf(KGRN "OK" KNRM "\n");

    printf("%-50s" KGRN "..." KNRM "\n", "Beginning VNA operation...");
    for(i=0; i<n_steps; i++) {
        printf("Setting frequency to %09uHz...%n", freq, &pos);
        printf("%*s", 50-pos, " ");
        fflush(stdout);
        result = bladerf_set_frequency(dev, BLADERF_MODULE_TX, freq);
        if(result < 0) goto error;
        result = bladerf_set_frequency(dev, BLADERF_MODULE_RX, freq - 1000);
        if(result < 0) goto error;
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Setting up TX stream...");
        fflush(stdout);
        tx_stream_data.num_buffers = num_buffers;
        tx_stream_data.samples_per_buffer = samples_per_buffer;
        tx_stream_data.samples_left = num_buffers * samples_per_buffer;
        tx_stream_data.num_transfers = num_transfers;
        tx_stream_data.next_buffer = 0;
        tx_stream_data.module = BLADERF_MODULE_TX;
        result = bladerf_init_stream(&tx_stream, dev, stream_cb,
                                     &(tx_stream_data.buffers),
                                     tx_stream_data.num_buffers,
                                     BLADERF_FORMAT_SC16_Q11,
                                     tx_stream_data.samples_per_buffer,
                                     tx_stream_data.num_transfers,
                                     &tx_stream_data);
        if(result < 0) goto error;
        tx_thread_data.dev = dev;
        tx_thread_data.stream = tx_stream;
        tx_thread_data.stream_data = &tx_stream_data;
        tx_thread_data.start = &threads_start;
        tx_thread_data.rv = 1;
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Filling transmit buffers...");
        fflush(stdout);
        for(j=0; j<num_buffers; j++) {
            int16_t *buf = tx_stream_data.buffers[j];
            for(k=0; k<samples_per_buffer; k++) {
                buf[2*k + 0] = 2047;
                buf[2*k + 1] = 2047;
            }
        }
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Setting up RX stream...");
        fflush(stdout);
        rx_stream_data.num_buffers = num_buffers;
        rx_stream_data.samples_per_buffer = samples_per_buffer;
        rx_stream_data.samples_left = num_buffers * samples_per_buffer;
        rx_stream_data.num_transfers = num_transfers;
        rx_stream_data.next_buffer = 0;
        rx_stream_data.module = BLADERF_MODULE_RX;
        result = bladerf_init_stream(&rx_stream, dev, stream_cb,
                                     &(rx_stream_data.buffers),
                                     rx_stream_data.num_buffers,
                                     BLADERF_FORMAT_SC16_Q11,
                                     rx_stream_data.samples_per_buffer,
                                     rx_stream_data.num_transfers,
                                     &rx_stream_data);
        if(result < 0) goto error;
        rx_thread_data.dev = dev;
        rx_thread_data.stream = rx_stream;
        rx_thread_data.stream_data = &rx_stream_data;
        rx_thread_data.start = &threads_start;
        rx_thread_data.rv = 1;
        printf(KGRN "OK" KNRM "\n");

        threads_start = 0;

        printf("%-50s", "Creating TX thread...");
        fflush(stdout);
        result = pthread_create(&tx_thread_pth, NULL,
                                txrx_thread, &tx_thread_data);
        if(result) {
            result = BLADERF_ERR_UNEXPECTED;
            goto error;
        }
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Creating RX thread...");
        fflush(stdout);
        result = pthread_create(&rx_thread_pth, NULL,
                                txrx_thread, &rx_thread_data);
        if(result) {
            result = BLADERF_ERR_UNEXPECTED;
            goto error;
        }
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Starting streams...");
        fflush(stdout);
        threads_start = 1;
        printf(KGRN "OK" KNRM "\n");

        printf("%-50s", "Waiting for completion...");
        fflush(stdout);
        pthread_join(rx_thread_pth, NULL);
        pthread_join(tx_thread_pth, NULL);
        printf(KGRN "OK" KNRM "\n");
        printf("%-50s", "Completed. Checking results...");
        fflush(stdout);

        if(tx_thread_data.rv < 0) {
            printf("Error in TX thread\n");
            result = BLADERF_ERR_UNEXPECTED;
            goto error;
        }

        if(rx_thread_data.rv < 0) {
            printf("Error in RX thread\n");
            result = BLADERF_ERR_UNEXPECTED;
            goto error;
        }
        printf(KGRN "OK" KNRM "\n");

        usleep(15000);

        printf("%-50s", "Computing RX power...");
        fflush(stdout);

        rx_power = 0.0f;
        /*printf("\n");*/
        for(j=1; j<(2*num_buffers)/3; j++) {
            double buffer_power = 0.0f;
            int16_t* buffer = (int16_t*)rx_stream_data.buffers[j];
            for(k=0; k<samples_per_buffer; k++) {
                double iq_i, iq_q;
                buffer[2*k] &= 0x0fff;
                if(buffer[2*k] & 0x0800)
                    buffer[2*k] |= 0xf000;
                buffer[2*k+1] &= 0x0fff;
                if(buffer[2*k+1] & 0x0800)
                    buffer[2*k+1] |= 0xf000;
                iq_i = (double)buffer[2*k];
                iq_q = (double)buffer[2*k+1];
                buffer_power += iq_i * iq_i + iq_q * iq_q;
                buffer[2*k] = buffer[2*k+1] = 0;
            }
            /*printf("Buffer %d: avg power %f\n", j, buffer_power / samples_per_buffer);*/
            rx_power += buffer_power / samples_per_buffer;
        }
        printf(KGRN "OK" KNRM "\n");

        tx_freqs[i] = freq;
        rx_powers[i] = rx_power / num_buffers;

        printf("\n");
        
        freq += f_step;
        bladerf_deinit_stream(tx_stream);
        bladerf_deinit_stream(rx_stream);
    }

    printf("\n\nResults:\nFREQUENCY (HZ)       POWER\n");
    for(i=0; i<n_steps; i++) {
        printf("%0.09f  %0.09f\n", tx_freqs[i], rx_powers[i]);
    }

    printf("Done, exiting.\n");
    result = 0;
    goto cleanup;

    error:
    printf(KRED "ERR" KNRM "\nBladeRF Error: %d %s\n",
           result, bladerf_strerror(result));
    result = 1;

    cleanup:
    if(tx_freqs) free(tx_freqs);
    if(rx_powers) free(rx_powers);
    bladerf_enable_module(dev, BLADERF_MODULE_RX, false);
    bladerf_enable_module(dev, BLADERF_MODULE_TX, false);
    bladerf_close(dev);
    bladerf_free_device_list(devices);

    return result;
}
