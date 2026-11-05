# TODO - something going wrong with variable layout.
.section .sbss
.align 3
.globl gReverbVoices
gReverbVoices:
  .space 8
.globl gAwaitingKeyOn
gAwaitingKeyOn:
  .space 8
.globl gAwaitingKeyOff
gAwaitingKeyOff:
  .space 8
.globl gKeyedOnVoices
gKeyedOnVoices:
  .space 8
.globl gKeyedOffVoices
gKeyedOffVoices:
  .space 8
.globl gTimerInfo
gTimerInfo:
  .space 8
