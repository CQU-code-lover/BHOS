;中断系统
KERN_DATA_SELECTOR equ 0x0002<<3 + 000b
KERN_VGA_SELECTOR equ 0x0003<<3 + 000b
;中断参数保存在ebx中！！！！！
[bits 32]
%macro NO_ERROCODE 1     ;带中断号参数宏
[GLOBAL isr%1]
isr%1:
;		mov eax,esp      ;中断参数传入
		push 0
		push %1          ;中断号
		jmp pre_handle
%endmacro

%macro HAVE_ERROCODE 1
[GLOBAL isr%1]
isr%1:
;		mov eax,esp
		nop
		push %1
		jmp pre_handle
%endmacro

;这个中断流程是内核与用户共用的
[EXTERN int_func_route]
[EXTERN print_debug_1]
pre_handle:
	pushad               ;压入八个32位
	mov ecx,[ss:esp+32]
	;mov ebx,eax
	mov ax,es          ;进入时ss已经被切换了
	and eax,0x0000FFFF
	push eax
	mov ax,fs          
	and eax,0x0000FFFF
	push eax
	mov ax,gs
	and eax,0x0000FFFF
	push eax
;切换回内核的中断相关段寄存器  cs与ss不用管 在中断时已经被cpu自动从tss中切换
	mov ax,KERN_DATA_SELECTOR    ;不包含ss
	mov es,ax
	mov fs,ax
	mov ax,KERN_VGA_SELECTOR
	mov gs,ax
	push ebx     ;传入void *
	push ecx     ;传入int类型中断号

; 此时的内核栈结构
;  high addr :       cpu填充结构
;                               错误号或0填充
; 								中断号
;                               eax
;                               ecx 
;                               edx
;                               ebx
;                               esp                    
;                               ebp
;                               esi
;                               edi
;                              	es（扩展为32位）
;                               fs（扩展为32位）
;                               gs（扩展为32位）
;                               中断参数
; low addr            中断号
	call int_func_route
;以下部分为中断退出结构
;C遵循函数调用者处理函数压入参数问题 所以使用add esp,8去除压入的两个参数（intr   AND    args）
;使用函数handle_int_exit_stack的开始位置
	add esp,8
	pop eax
	mov gs,ax
	pop eax
	mov fs,ax
	pop eax
	mov es,ax
	popad
	add esp,8
	push eax
	mov al,0x20
	out 0xA0,al
	out 0x20,al
	pop eax
;此处的al是否需要保存？
;yes!!!!!!!!!!!!!
	iret


NO_ERROCODE 0 ;DE
NO_ERROCODE 1 ;DB
NO_ERROCODE 2 ;NMI
NO_ERROCODE 3 ;BP
NO_ERROCODE 4 ;OF
NO_ERROCODE 5 ;BR
NO_ERROCODE 6 ;UD
NO_ERROCODE 7 ;NM
HAVE_ERROCODE 8 ;DF
NO_ERROCODE 9
HAVE_ERROCODE 10 ;TS
HAVE_ERROCODE 11 ;NP
HAVE_ERROCODE 12 ;SS
HAVE_ERROCODE 13 ;GP
HAVE_ERROCODE 14 ;PF
NO_ERROCODE 15 ;RESERVE
NO_ERROCODE 16 ;MF
HAVE_ERROCODE 17 ;AC
NO_ERROCODE 18 ;MC
NO_ERROCODE 19 ;XM

;20 to 31 is reserved,we can use is to be software interrupt
NO_ERROCODE 20
NO_ERROCODE 21
NO_ERROCODE 22
NO_ERROCODE 23
NO_ERROCODE 24
NO_ERROCODE 25
NO_ERROCODE 26
NO_ERROCODE 27
NO_ERROCODE 28
NO_ERROCODE 29
NO_ERROCODE 30
NO_ERROCODE 31

;32 TO 47 IS IRQ(hardware interrupt)
NO_ERROCODE 32  ;system global clock(From 8259A)
NO_ERROCODE 33  ;keybord
NO_ERROCODE 34  ;For series of 8259A
NO_ERROCODE 35  ;Serial Port 2
NO_ERROCODE 36  ;Serial Port 1
NO_ERROCODE 37  ;Parallel Port 2
NO_ERROCODE 38  ;floppy
NO_ERROCODE 39  ;Parallel Port 1
NO_ERROCODE 40  ;clock
NO_ERROCODE 41  ;redirect
NO_ERROCODE 42  ;reserved
NO_ERROCODE 43  ;reserved
NO_ERROCODE 44  ;PS/2 mouse
NO_ERROCODE 45  ;FPU exception
NO_ERROCODE 46  ;hard disk
NO_ERROCODE 47  ;reserved
[GLOBAL load_idt]
load_idt:
	mov eax, [esp+4]  ; 参数存入 eax 寄存器
    lidt [eax]        ; 加载到 IDTR
    ret

[GLOBAL get_cr2]
[GLOBAL _CR2]
get_cr2:
	mov eax,cr2
	mov [_CR2],eax
	ret
_CR2:
	dd 0x00