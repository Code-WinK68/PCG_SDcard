# ESP32 PCG Data Acquisition & Real-Time Validation System

An optimized, highly stable Data Acquisition (DAQ) system for Phonocardiogram (PCG) signals using **ESP32-WROOM-32** and the **ICS-43434** digital MEMS microphone via I2S. The system features real-time edge synchronization verification and direct logging to a MicroSD card in a clean CSV format.

To eliminate race conditions, memory leaks, and FreeRTOS context-switching overhead, this system abandons the traditional multi-task/queue architecture in favor of a strictly sequential **Single-Task Architecture**.

---

## 📌 Key Features

* **High-Fidelity 16kHz Sampling**: Configured in I2S Philips Standard Mode, extracting 24-bit MSB audio samples packed inside 32-bit DMA slots.
* **Edge-Based Sync Verification**: Real-time evaluation of actual sample counts per 2.5ms hardware block to instantly detect Clock Drift.
* **Deterministic Single-Task Pipeline**: Read I2S $\rightarrow$ Validate Sync $\rightarrow$ Plot Data $\rightarrow$ Batch Log to SD. Everything executes sequentially inside a single infinite loop, using hardware DMA interrupts as the natural clock.
* **Zero false timeouts**: Enhanced with an expanded DMA ring buffer (8 descriptors × 240 frames) and a 200ms threshold to absorb I/O block latencies safely.
* **Robust Double-Buffered CSV Logging**: Accumulates 50 blocks (~125ms of data) in RAM before executing a bulk write down to the SD Card, minimizing SPI bus strain and maximizing card lifespan.

---

## 🛠 Hardware Connection & Pinout

The hardware wiring is designed specifically for the standard **ESP32-WROOM-32**. Note that **GPIO 34** is an *Input-Only* pin, making it perfect for receiving I2S digital audio data.

### 1. ICS-43434 Digital Microphone Configuration
| ICS-43434 Pin | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **BCLK** (Bit Clock) | **GPIO 26** | I2S Bit Clock Output |
| **WS** (Word Select) | **GPIO 25** | I2S Left/Right Clock Output |
| **SD** (Serial Data) | **GPIO 34** | I2S Data Input (Input-Only Pin) |
| **LR** (Left/Right) | **GND** | Pulled to GND to select the LEFT channel (Mono) |
| **3V3 & GND** | **3V3 & GND** | Clean power rails |

### 2. MicroSD Card Slot Configuration (SPI Mode)
| MicroSD Pin | ESP32 Pin | SPI Function |
| :--- | :--- | :--- |
| **CS** | **GPIO 15** | Chip Select |
| **MOSI** | **GPIO 13** | Master Out Slave In |
| **MISO** | **GPIO 12** | Master In Slave Out |
| **CLK** | **GPIO 14** | Serial Clock |

---

## 📐 Firmware Pipeline Architecture

By avoiding FreeRTOS multi-threading primitives (Queues, Semaphores, Mutexes), the entire pipeline operates safely without any thread contention or layout breakage:

-
========================================================================
                 Infinite Loop: task_pcg_all_in_one
========================================================================
                               |
                               v
       +-----------------------------------------------+
       | 1. i2s_channel_read() (Blocking ~2.5ms)       |
       |    - Waits for hardware DMA for 40 samples    |
       +-----------------------------------------------+
                               |
                               v
       +-----------------------------------------------+
       | 2. Edge-Validation Logic                      |
       |    - Checks if |actual_count - 40| <= 2       |
       |    - Increments good_count / bad_count flags  |
       +-----------------------------------------------+
                               |
                               v
       +-----------------------------------------------+
       | 3. Serial Plotter Stream (UART @921600 Baud)  |
       |    - Formats: PCG:[scaled_val]  SYNC_OK:[1/0] |
       +-----------------------------------------------+
                               |
                               v
       +-----------------------------------------------+
       | 4. Batch Buffer Accumulation                  |
       |    - Converts raw data to 40-column CSV style |
       |    - Triggers fwrite() to SD every 50 blocks  |
       +-----------------------------------------------+

---
## 📊 Data & Output Visualization

### 1. VS Code Serial Plotter
Opening the Serial Plotter at **921600 Baud** displays two continuous channels:
* `PCG`: The acoustic heart waveform, cleanly scaled down by a factor of $\gg 12$ to properly fit the viewport dynamics.
* `SYNC_OK`: A straight status line resting at `1`. If hardware clock drift or dropped samples occur, it immediately drops to `0`.

### 2. MicroSD Logging Format (`pcg_data.csv`)
Data is saved directly onto the root folder of your card. Each row contains a precise timestamp alongside 40 columns of raw 24-bit audio values:
```csv
block_index,timestamp_us,actual_count,sync_flag,pcg_1,pcg_2,...,pcg_40
1,1002500,40,GOOD,1542,-321,...,450
2,1005000,40,GOOD,1420,-290,...,390
3,1007500,38,BAD,910,-120,...,210
