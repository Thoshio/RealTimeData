import serial
import re
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

# --- Configurações da Porta Serial ---
# Altere para a porta correspondente no seu sistema opercional
PORTA_SERIAL = 'COM8' 
BAUD_RATE = 115200 # Baud rate padrão do console do Zephyr

# --- Configurações do Gráfico ---
MAX_PONTOS = 100 # Quantidade de amostras visíveis na tela simultaneamente

# Filas (deques) para armazenar os dados e criar o efeito de "rolagem" (scrolling)
x_data = deque(range(MAX_PONTOS), maxlen=MAX_PONTOS)
y_raw = deque([0] * MAX_PONTOS, maxlen=MAX_PONTOS)
y_mv = deque([0] * MAX_PONTOS, maxlen=MAX_PONTOS)

# Expressão regular para capturar os números da string enviada pelo Zephyr
# Esperado: "[<timestamp>] ADC: <raw> (raw), <mv> mV"
regex_dados = re.compile(r"\[(\d+)\]\s*ADC:\s*(\d+)\s*\(raw\),\s*(\d+)\s*mV")

# Inicialização da conexão serial
try:
    ser = serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=0.1)
    print(f"Conectado à porta {PORTA_SERIAL} com sucesso.")
except serial.SerialException as e:
    print(f"Erro ao conectar na porta serial: {e}")
    exit()

# Configuração da figura e dos subgráficos
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
fig.suptitle('Leitura do ADC em Tempo Real - FRDM KL25z', fontsize=14)

# Gráfico 1: Valor Raw
linha_raw, = ax1.plot(x_data, y_raw, color='blue', label='Valor Raw (12 bits)')
ax1.set_ylim(0, 4150) # O ADC está configurado para 12 bits (0 a 4095)
ax1.set_ylabel('Amplitude (Raw)')
ax1.grid(True, linestyle='--', alpha=0.6)
ax1.legend(loc='upper right')

# Gráfico 2: Tensão em milivolts
linha_mv, = ax2.plot(x_data, y_mv, color='red', label='Tensão (mV)')
ax2.set_ylim(0, 3400) # Referência configurada para 3300 mV
ax2.set_ylabel('Tensão (mV)')
ax2.set_xlabel('Amostras')
ax2.grid(True, linestyle='--', alpha=0.6)
ax2.legend(loc='upper right')

def atualizar_grafico(frame):
    """Função chamada periodicamente pelo FuncAnimation para atualizar os dados."""
    if ser.in_waiting > 0:
        try:
            # Lê uma linha da serial, decodifica e remove espaços em branco extras
            linha_serial = ser.readline().decode('utf-8').strip()
            
            # Verifica se a linha lida corresponde ao padrão esperado
            match = regex_dados.search(linha_serial)
            if match:
                raw_val = int(match.group(2))
                mv_val = int(match.group(3))
                
                # Adiciona os novos valores à fila (removendo os mais antigos automaticamente)
                y_raw.append(raw_val)
                y_mv.append(mv_val)
                
                # Atualiza os dados das linhas no gráfico
                linha_raw.set_ydata(y_raw)
                linha_mv.set_ydata(y_mv)
        except UnicodeDecodeError:
            # Ignora erros de decodificação causados por ruído na comunicação inicial
            pass
            
    return linha_raw, linha_mv

# Configuração da animação (atualiza a cada 50 ms)
ani = animation.FuncAnimation(fig, atualizar_grafico, interval=50, blit=True, cache_frame_data=False)

# Exibe a janela gráfica
plt.tight_layout()
plt.show()

# Fecha a porta serial quando a janela gráfica for fechada
ser.close()
print("Conexão serial encerrada.")