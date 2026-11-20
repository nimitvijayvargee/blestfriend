<img width="1225" height="162" alt="image" src="https://github.com/user-attachments/assets/07225cb4-be5a-4f46-a37d-3def790b2a6d" />

---

this is a basic devboard I made to practice soldering SMD components better, as well as to help me create audio sink/source projects which I have plans for :3
it essentially uses an ESP32-WROOM-32E module, which supports wifi, BLE and BT classic,, which are all quite useful for general projects and iot applications. 

besides that, it's pretty much a standard ESP32 devboard with a UART link.

## PCB
<img width="3508" height="2480" alt="image" src="https://github.com/user-attachments/assets/72391a1d-8fc2-4708-94bb-077ef8517baa" />
<img width="996" height="840" alt="image" src="https://github.com/user-attachments/assets/d7b4ceca-b7f4-4dbe-bda8-799a9b32248f" />
<img width="1712" height="896" alt="image" src="https://github.com/user-attachments/assets/664a264e-e8b1-4bdb-a96e-98147b2c74c6" />


# BOM
Here's a rough list of components you need to actually finish this! All SMD passives are 0805 unless specified otherwise

- PCB (1.6mm thickness FR4 2 Layer board, other settings up to your discretion!)
- USB C Receptacle (HRO TYPE-C-31-M-14)
- 2x BAT60A SOD323 Diodes
- AP1117-33 Linear Regulator SOT223-3
- ESP32-WROOM-32E OR ESP32-WROOM-32UE (UE needs external antenna)
- CH340C SOIC16 (Other CH340 variants needing a crystal will not work!)
- 6mm Tactile Push Button
Passives:
- 4x 100nF Ceramic Capacitors
- 4x 10uF Ceramic Capacitors
- 2x 100k Resistors
- 2x 200k Resistors
- 2x 5.1k Resistors (CC Pulldown)
- 1x 10k Resistor

You can also find a more thorough BOM with URLs for sellers in India in BOM.csv.
