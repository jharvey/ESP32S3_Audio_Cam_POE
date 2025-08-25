# ESP32S3_Audio_Cam_POE

This is a PCB layout for ESP32S3 with OV2640 / OV5640 camera. This was designeed with KiCAD version 9. 
The boards major features include:
* Camera.
* 2 way audio.
* Hardwired Ethernet including POE.
* Camera pan / tilt servos and lamp control.
* record to SD card.
* misc outputs to drive misc motors or similar outputs. 
* Battery powered option with Lion type of battery. 
* Stream video to NVR recording station. 

## Firmware
Firmware found here https://github.com/jharvey/ESP32-CAM_MJPEG2SD


Changes up to version 0.1:
* Initial layout

## Purpose

The layout design intent is to make a hardwired intercom system which includes options for camera, making a near equivilant of a doorbell camera. Hardwired means it's not susceptibale to RF jaming, or similar attacks. It can be battery powered at the ethernet switch source so it can handle power outagaged. Lighting controls allow night time spot light, and day time spot light controls. Pan, Tilt, Rotate could potentailly allow a laser pointer to distract bad players if things go bump in the night. 


## Installation
After cloning, don't forget to check out the submodule. 


TBD currently has not had it's first spin. TBD


## 3D case
See 3D case found here
TBD create file in OnShape and post link here. TBD

## 3D pictures from KICAD
<div align="center">
<img src="ESP32S3_Audio_Cam_POE_Back.png" alt="ESP32S3_Cam_POE_Back" width="600" />
<img src="ESP32S3_Audio_Cam_POE_Front.png" alt="ESP32S3_Cam_POE_Front" width="600" />
<div align="left">