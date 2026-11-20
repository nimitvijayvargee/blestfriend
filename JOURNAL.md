---
title: "blestfriend"
github: "https://github.com/nimitvijayvargee/blestfriend"
description: "esp32 devboard! made this to practice on my hot plate :)"
created_at: "2026-11-20"
---

# Nov 17
I did a ton of research! My main motivation for making this devboard was A2DP, which is a protocol used by bluetooth classic devices to transmit audio. most microcontrollers on the market either don't support A2DP, or they do but with terrible software integration. The main reason is because it is easier to sacrifice bluetooth classic in favor of bluetooth LE, which has its own LE audio. However, a lot of low quality audio equipment, or cheaper devices don't really have LE audio support. 

I settled on the ESP32-WROOM modules to keep the cost low, as it doesn't need as many additional components (like flash) to run, so I only need to add a UART link and some other smaller electronics. I also recently got an SMD hot plate, so this would be my first project to handsolder! 

I also wanted to make this more compact than other ESP32 modules, as most of them are really huge. This is quite difficult to do in terms of width, because of just how large the WROOM is. But lengthwise, it's not too difficult to keep it compact.

**Time Spent: 1 hour**

# Nov 18
Worked on the schematic a bit, this is quite straight forward! A USB C port with two 5.1k CC pulldowns (to make the device request power from the host), and an LDO. I added a BAT60A between the LDO, which has a quite low voltage dropoff. This lets me add another VBAT pin that lets the user insert their own external power source. However, due to the ~250mV forward voltage of the diode, the external battery may need to have a higher voltage, or use an external regulator to bring the voltage to a usable range. 

This was around the time I realized I actually needed to put in a CH340 ic to convert my USB to UART. I didn't realize the WROOM didn't have any USB support, and could only be programmed over UART :/
 
To make my life easier, I just put in a CH340C (which doesn't need a crystal) and moved onto the rest of the stuff.

I quickly finished the rest of the schematic and added the capacitors. For the headers a 15x2 layout seemed reasonable, so put random nets and sent it to the editor. I just wanted to get a feel of the layout and pin locations before assigning any specific labels.

<img width="3508" height="2480" alt="image" src="https://github.com/user-attachments/assets/72391a1d-8fc2-4708-94bb-077ef8517baa" />

**Time Spent: 3 hours**

# Nov 19
PCB time! I made the PCB for the devboard quite quickly. Keep the ESP32 near the middle, put the USB port at the opposite end and everything else in between. I also worked on routing it a couple times, but not much progress was made this day :c

<img width="584" height="640" alt="image" src="https://github.com/user-attachments/assets/bc903336-25cc-4a79-ba2b-9ad12a71b9a4" />

**Time Spent: 1 hour**

# Nov 20
Big day! I want to ship this project TODAY. I quickly wrapped up the routing, and realized having the GPIO in order is a bad idea and will use very poor routing. I referred to the datasheet and edited the nets on the headers to match the module. 

After that, I routed it a few more times to get it just right. I created a quick logo (pixelated wireless symbol, and the two peeps holding hands) and slapped it onto the PCB with a pixelated font to match. I kinda like the logo, so I brought it into my image editor to give it a splash of colour and put in my README while working on it.

I published everything to github and started working on the BOM. This was pretty easy, I just had to find the parts on robu.in (i love robu <3) although I had to source the CH340C from an alternate seller due to stock. My BOM comes in to just over $80, but I can bring it down using my robu loyalty points (i love robu <3).

I also experimented with adding an LED to the board, but I eventually decided on not doing that because the only addressible LEDs I had were 3.7V at minimum, so it would need a lot more components to be reliable.

Anyway I finally worked on this journal for a bit and I think this project is ready to submit! :D

<img width="996" height="840" alt="image" src="https://github.com/user-attachments/assets/d7b4ceca-b7f4-4dbe-bda8-799a9b32248f" />
<img width="1712" height="896" alt="image" src="https://github.com/user-attachments/assets/664a264e-e8b1-4bdb-a96e-98147b2c74c6" />

**Time Spent: 4 hours**

<3