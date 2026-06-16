#include <zephyr/kernel.h>             // Funções básicas do Zephyr (ex: k_msleep, k_thread, etc.)
#include <zephyr/device.h>             // API para obter e utilizar dispositivos do sistema
#include <zephyr/drivers/gpio.h>       // API para controle de pinos de entrada/saída (GPIO)
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <pwm_z402.h>

#define TPM_MODULE 37500           // Define a frequência do PWM fpwm = (TPM_CLK / (TPM_MODULE * PS)) para um periodo de 100ms
#define DUTY_CYCLE TPM_MODULE/2     // Define duty_cycle em 50%  ->  pulsos duram 50ms 

// LOG
LOG_MODULE_REGISTER(meu_modulo, LOG_LEVEL_WRN);

// ADC
#define ADC_RESOLUTION      12
#define ADC_GAIN            ADC_GAIN_1
#define ADC_REFERENCE       ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID      0  //Canal do ADC, veja a pinagem
#define ADC_VREF_MV         3300

static int16_t sample_buffer = 0;
static int32_t mv = 0;
static uint32_t timestamp_us = 0;

// Threads
#define STACK_SIZE 1024
#define PRIORITY 5

K_THREAD_STACK_DEFINE(stack_adc, STACK_SIZE);
struct k_thread adc;
void thread_ADC (void *arg1, void *arg2, void *arg3) {
    while (1) {
        // ADC
        int err = adc_read(arg1, arg2); // Faz leitura no adc
        if (err != 0) {
            printk("Falha na leitura do ADC: %d\n", err);
        } else {            
            // Registra o tempo exato em microssegundos logo após a leitura
            timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());;
            
            // Transforma em milivolts
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
    printk("[%u] ADC: %d (raw), %d mV\n", timestamp_us, sample_buffer, mv);
    
    //Wait
    k_msleep(100);
    }
}


int main(void)
{
    /*                  PWM                 */
    //TPM2 Ch:0 PTB2
    pwm_tpm_Init(TPM2, TPM_PLLFLL, TPM_MODULE, TPM_CLK, PS_128, EDGE_PWM);
    pwm_tpm_Ch_Init(TPM2, 0, TPM_PWM_H, GPIOB, 2);
    pwm_tpm_CnV(TPM2, 0, DUTY_CYCLE);



    /*                  ADC                 */  
    const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (!device_is_ready(adc_dev)) {
        printk("ADC não está pronto\n");
        return 1;
    }

    // PTE20
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