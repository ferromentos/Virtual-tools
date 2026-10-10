    .data
    .globl ds_real
ds_real:
    .space 128
    .text
    .macro fwd name idx
    .globl \name
\name:
    jmp *ds_real+\idx*8(%rip)
    .endm
    fwd DirectSoundCreate 0
    fwd DirectSoundEnumerateA 1
    fwd DirectSoundEnumerateW 2
    fwd DllCanUnloadNow 3
    fwd DllGetClassObject 4
    fwd DirectSoundCaptureCreate 5
    fwd DirectSoundCaptureEnumerateA 6
    fwd DirectSoundCaptureEnumerateW 7
    fwd GetDeviceID 8
    fwd DirectSoundFullDuplexCreate 9
    fwd DirectSoundCreate8 10
    fwd DirectSoundCaptureCreate8 11
    fwd DllRegisterServer 12
    fwd DllUnregisterServer 13
