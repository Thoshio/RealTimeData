#include <zephyr/kernel.h>             // Funções básicas do Zephyr (ex: k_msleep, k_thread, etc.)
#include <zephyr/device.h>             // API para obter e utilizar dispositivos do sistema
#include <zephyr/drivers/gpio.h>       // API para controle de pinos de entrada/saída (GPIO)
#include <zephyr/drivers/sensor.h>     // API para sensores do Zephyr
#include <zephyr/drivers/adc.h>

// Acelerometro
// Obter referência do dispositivo usando o nodelabel
static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(mma8451q));
uint32_t tempo_ms = 0;  // Contador de tempo em milissegundos
struct sensor_value accel_x, accel_y, accel_z;

// ADC
#define ADC_RESOLUTION      12
#define ADC_GAIN            ADC_GAIN_1
#define ADC_REFERENCE       ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID      0  //Canal do ADC, veja a pinagem
#define ADC_VREF_MV         3300

static int16_t sample_buffer;


// Botão
#define BUTTON_NODE DT_NODELABEL(user_button_0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;

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
            int32_t mv = sample_buffer;
            adc_raw_to_millivolts(ADC_VREF_MV, ADC_GAIN, ADC_RESOLUTION, &mv);
            printk("ADC: %d (raw), %d mV\n", sample_buffer, mv);
        }

        //Wait
        k_msleep(500);
    }
}

K_THREAD_STACK_DEFINE(stack_acelerometro, STACK_SIZE);
struct k_thread acelerometro;
void thread_acelerometro (void *arg1, void *arg2, void *arg3) {
    while (1) {
        // Solicitar leitura do sensor
        int ret = sensor_sample_fetch(accel);
        if (ret) {
            printk("Erro ao ler sensor: %d\n", ret);
            k_msleep(1000);
            tempo_ms += 1000;
            continue;
        }

        // Obter valores dos eixos X, Y e Z
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &accel_x);
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &accel_y);
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &accel_z);

        // Formato: T: tempo_ms, X: valor, Y: valor, Z: valor
        printk("T: %u, X: %d.%06d, Y: %d.%06d, Z: %d.%06d\r\n", 
               tempo_ms,
               accel_x.val1, abs(accel_x.val2),
               accel_y.val1, abs(accel_y.val2),
               accel_z.val1, abs(accel_z.val2));

        // Aguardar 1000ms antes da próxima leitura
        k_msleep(1000);
        tempo_ms += 1000;
    }
}



// ISR - Botao
void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{   
    static int press = 0;

    if (press == 0) {
        k_thread_suspend(&acelerometro);
        press = !press;
    } else {
        k_thread_resume(&acelerometro);
        press = !press;
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
                    3, 0, K_NO_WAIT);

    ///////////////////////////////////////////          


    /*               ACELEROMETRO                */
    printk("Iniciando Acelerômetro\n");

    // Verificar se o dispositivo está pronto
    if (!device_is_ready(accel)) {
        printk("ERRO: Acelerometro nao esta pronto!\n");
        return 1;
    }

    printk("Acelerometro inicializado com sucesso!\n");
    printk("Iniciando leituras a cada 500ms...\n\n");

    // Criando thread
    k_thread_create(&acelerometro, stack_acelerometro,
                    K_THREAD_STACK_SIZEOF(stack_acelerometro),
                    thread_acelerometro,
                    NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);
    
    //////////////////////////////////////////////////////////////

    /*                     BOTAO                     */
    // Configurar LED e botão com pull-up
    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    
    // Configurar interrupção na borda de descida
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    ///////////////////////////////////////////////////////////////


    while (1) {

        k_sleep(K_FOREVER);
        
    }

    return 0;
}