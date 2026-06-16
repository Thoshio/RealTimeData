#include <zephyr/kernel.h>             // Funções básicas do Zephyr (ex: k_msleep, k_thread, etc.)
#include <zephyr/device.h>             // API para obter e utilizar dispositivos do sistema
#include <zephyr/drivers/gpio.h>       // API para controle de pinos de entrada/saída (GPIO)
#include <zephyr/drivers/adc.h>

// ADC
#define ADC_RESOLUTION      12
#define ADC_GAIN            ADC_GAIN_1
#define ADC_REFERENCE       ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID      0  //Canal do ADC, veja a pinagem
#define ADC_VREF_MV         3300

static int16_t sample_buffer = 0;
static int32_t mv = 0;

// Threads
#define STACK_SIZE 1024
#define PRIORITY 5

K_THREAD_STACK_DEFINE(stack_adc, STACK_SIZE);
struct k_thread adc;
void thread_ADC (void *arg1, void *arg2, void *arg3) {
    while (1) {
        // ADC
        int err = adc_read(arg1, arg2);
        if (err != 0) {
            printk("Falha na leitura do ADC: %d\n", err);
        } else {
            mv = sample_buffer;
            adc_raw_to_millivolts(ADC_VREF_MV, ADC_GAIN, ADC_RESOLUTION, &mv);
        }

        //Wait
        k_msleep(100);
    }
}

K_THREAD_STACK_DEFINE(stack_com, STACK_SIZE);
struct k_thread com;
void thread_comunicate (void *arg1, void *arg2, void *arg3) {
    while (1) {
    printk("ADC: %d (raw), %d mV\n", sample_buffer, mv);
    
    //Wait
    k_msleep(100);
    }
}


int main(void)
{
    /*                  ADC                 */  
    const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (!device_is_ready(adc_dev)) {
        printk("ADC não está pronto\n");
        return 1;
    }

    struct adc_channel_cfg channel_cfg = {
        .gain = ADC_GAIN,
        .reference = ADC_REFERENCE,
        .acquisition_time = ADC_ACQUISITION_TIME,
        .channel_id = ADC_CHANNEL_ID,
        .differential = 0,
    };

    if (adc_channel_setup(adc_dev, &channel_cfg) != 0) {
        printk("Erro ao configurar canal ADC\n");
        return 1;
    }

    struct adc_sequence sequence = {
        .channels    = BIT(ADC_CHANNEL_ID),
        .buffer      = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution  = ADC_RESOLUTION,
    };

    // Criando thread
    k_thread_create(&adc, stack_adc,
                    K_THREAD_STACK_SIZEOF(stack_adc),
                    thread_ADC,
                    (void *)adc_dev, &sequence, NULL,
                    PRIORITY, 0, K_NO_WAIT);


                    

    /*                  Comunication                 */ 
    // Criando thread
    k_thread_create(&com, stack_com,
                    K_THREAD_STACK_SIZEOF(stack_adc),
                    thread_comunicate,
                    NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);

    // Loop main
    while (1) {

        k_sleep(K_FOREVER);
        
    }

    return 0;
}