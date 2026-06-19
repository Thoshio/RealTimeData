import serial
import re
import matplotlib.pyplot as plt
from collections import deque
import time

# --- Configurações da Porta Serial ---
PORTA_SERIAL = 'COM8'  # Substitua pela porta correta
BAUD_RATE = 115200

# --- Configurações do Gráfico ---
MAX_PONTOS = 100

x_tempo = deque(maxlen=MAX_PONTOS)
y_raw = deque(maxlen=MAX_PONTOS)
y_filt = deque(maxlen=MAX_PONTOS)

tempo_inicial_us = None

# Regex para os dados do ADC
regex_dados = re.compile(r"\[(\d+)\s*us\]\s*ADC:\s*(-?\d+)\s*mV,\s*(-?\d+)\s*mV_filter")

# NOVA Regex para detectar o log de pacotes perdidos do Zephyr
regex_erro = re.compile(r"--- (\d+) messages dropped ---")

# Inicialização da conexão serial
try:
    ser = serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=0.01)
    print(f"Conectado à porta {PORTA_SERIAL} com sucesso.")
except serial.SerialException as e:
    print(f"Erro ao conectar na porta serial: {e}")
    exit()

# Ativa o modo interativo do Matplotlib
plt.ion()

fig, ax = plt.subplots(figsize=(10, 5))
fig.suptitle('Filtro FIR em Tempo Real (Loop de Alta Performance)', fontsize=14)

linha_raw, = ax.plot([], [], color='blue', alpha=0.4, label='Sinal Bruto (mV)')
linha_filt, = ax.plot([], [], color='red', linewidth=2, label='Sinal Filtrado (mV)')

ax.set_ylim(-800, 5000)  
ax.set_ylabel('Tensão (mV)')
ax.set_xlabel('Tempo Relativo (segundos)')
ax.grid(True, linestyle='--', alpha=0.6)
ax.legend(loc='upper right')

# Renderiza a estrutura do gráfico inicial antes do loop
plt.show()

# --- Loop Principal (Tempo Real Genuíno) ---
try:
    # O loop roda o mais rápido possível enquanto a janela do gráfico existir
    while plt.fignum_exists(fig.number):
        houve_atualizacao = False
        
        # Esvazia tudo o que chegou na serial
        while ser.in_waiting > 0:
            try:
                linha_serial = ser.readline().decode('utf-8').strip()
                
                # Tenta casar a linha com o padrão de dados
                match_dados = regex_dados.search(linha_serial)
                
                if match_dados:
                    timestamp_atual_us = int(match_dados.group(1))
                    mv_raw = int(match_dados.group(2))
                    mv_filtered = int(match_dados.group(3))
                    
                    if tempo_inicial_us is None:
                        tempo_inicial_us = timestamp_atual_us
                    
                    tempo_segundos = (timestamp_atual_us - tempo_inicial_us) / 1_000_000.0
                    
                    x_tempo.append(tempo_segundos)
                    y_raw.append(mv_raw)
                    y_filt.append(mv_filtered)
                    
                    houve_atualizacao = True
                
                else:
                    # Se não for dado numérico, verifica se é uma mensagem de perda
                    match_erro = regex_erro.search(linha_serial)
                    if match_erro:
                        qtd_perdida = match_erro.group(1)
                        print(f"⚠️ ALERTA DO ZEPHYR: {qtd_perdida} mensagens descartadas por sobrecarga na serial!")
                    
            except UnicodeDecodeError:
                pass 
                
        # Atualiza a tela APENAS se novos dados foram recebidos e decodificados
        if houve_atualizacao and len(x_tempo) > 0:
            linha_raw.set_data(x_tempo, y_raw)
            linha_filt.set_data(x_tempo, y_filt)
            
            # Move o eixo X instantaneamente
            ax.set_xlim(x_tempo[0], x_tempo[-1] + 0.05)
            
            # Força o redesenho imediato do canvas da interface gráfica
            fig.canvas.draw()
            fig.canvas.flush_events()
            
        # Um micro-descanso de 1 milissegundo para evitar que o Python consuma 100% da CPU do seu PC
        time.sleep(0.001)

except KeyboardInterrupt:
    print("\nExecução interrompida no terminal.")
finally:
    ser.close()
    print("Conexão serial encerrada de forma segura.")