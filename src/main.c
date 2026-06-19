#include <zephyr/kernel.h>             // Funções básicas do Zephyr (ex: k_msleep, k_thread, etc.)
#include <zephyr/device.h>             // API para obter e utilizar dispositivos do sistema
#include <zephyr/drivers/gpio.h>       // API para controle de pinos de entrada/saída (GPIO)
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <pwm_z402.h>

// LOG
LOG_MODULE_REGISTER(meu_modulo, LOG_LEVEL_INF);

#define TEMPO 9999

// Select com ou sem filtro
#define FILTRO 1

// PWM
#define TPM_MODULE 37500           // Define a frequência do PWM fpwm = (TPM_CLK / (TPM_MODULE * PS)) para um periodo de 100ms
#define DUTY_CYCLE TPM_MODULE/2    // Define duty_cycle em 50%  ->  pulsos duram 50ms 

// ADC
#define ADC_RESOLUTION      12
#define ADC_GAIN            ADC_GAIN_1
#define ADC_REFERENCE       ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID      0  // PTE20
#define ADC_VREF_MV         3300

// Variáveis
static int16_t sample_buffer = 0;
static int32_t mv = 0;
static int32_t mv_filtered = 0;
static uint32_t timestamp_us = 0;

// Semáforo para sincronizar a leitura do ADC com a execução do filtro
K_SEM_DEFINE(sem_adc_ready, 0, 1);

// Filtro FIR
#define NUM_TAPS 49
// Coeficientes do filtro (fs=100Hz, fc=3Hz, Kaiser Beta=4.0)
const float fir_taps[NUM_TAPS] = {0.007796f, 0.013162f, 0.013760f, 0.008909f, -0.000000f, -0.009847f, -0.016818f, -0.017808f, -0.011694f, 0.000000f, 0.013364f, 0.023287f, 0.025228f, 0.017009f, -0.000000f, -0.020789f, -0.037841f, -0.043247f, -0.031183f, 0.000000f, 0.046774f, 0.100910f, 0.151365f, 0.187098f, 0.200000f, 0.187098f, 0.151365f, 0.100910f, 0.046774f, 0.000000f, -0.031183f, -0.043247f, -0.037841f, -0.020789f, -0.000000f, 0.017009f, 0.025228f, 0.023287f, 0.013364f, 0.000000f, -0.011694f, -0.017808f, -0.016818f, -0.009847f, -0.000000f, 0.008909f, 0.013760f, 0.013162f, 0.007796f};
static float buffer_amostras[NUM_TAPS] = {0};

// Threads
#define STACK_SIZE 1024
#define PRIORITY 5


/*                  ADC                 */
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

        // Libera o filtro para processar a nova amostra
        k_sem_give(&sem_adc_ready);

        //Wait
        k_sleep(K_USEC(TEMPO));
    }
}




/*                  FILTRO                 */
K_THREAD_STACK_DEFINE(stack_filter, STACK_SIZE);
struct k_thread filter;
void thread_FILTER (void *arg1, void *arg2, void *arg3) {
    while (1) {
        // Aguarda a thread do ADC liberar uma nova amostra
        k_sem_take(&sem_adc_ready, K_FOREVER);

        // Desloca o buffer circular (shift)
        for (int i = NUM_TAPS - 1; i > 0; i--) {
            buffer_amostras[i] = buffer_amostras[i - 1];
        }
        
        // Insere a nova amostra na posição mais recente
        buffer_amostras[0] = (float)mv;

        // Calcula a convolução FIR
        float acumulador = 0.0f;
        for (int i = 0; i < NUM_TAPS; i++) {
            acumulador += fir_taps[i] * buffer_amostras[i];
        }

        // Salva o resultado final convertido de volta para inteiro
        mv_filtered = (int32_t)acumulador;
        k_sleep(K_USEC(TEMPO));
    }
}




/*                  Comunication                 */ 
K_THREAD_STACK_DEFINE(stack_com, STACK_SIZE);
struct k_thread com;
void thread_comunicate (void *arg1, void *arg2, void *arg3) {
    while (1) {
    LOG_INF("[%u us] ADC: %d mV, %d mV_filter\n", timestamp_us, mv, mv_filtered);
    
    //Wait
    k_sleep(K_USEC(TEMPO));
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

    if (FILTRO == 1) {

    /*                  Filter                 */
    // Criando thread
    k_thread_create(&filter, stack_filter,
                    K_THREAD_STACK_SIZEOF(stack_filter),
                    thread_FILTER,
                    NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);
    }


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