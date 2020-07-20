;/boot/boot.s
;edit:2020/1/26
;by:不吃香菜的大头怪
;注意：临时页表只能使用到4M的内核
%include "./boot/boot.inc" 
;这三个双字为GRUB加载器识别MagicNumber 以及配置信息（可以不用理解）
section .init.text       ;这个节在文件开头所以要标示一下
dd 0x1badb002

dd 0x3   	
dd -(0x1badb002+0x3)

temp_mboot_ptr:      ;暂存ebx
    dd 0x0
[BITS 32]        ;GRUB已经帮助我们进入了保护模式 所以是可以使用32编译的

[GLOBAL boot_start]
boot_start:        ;此处是内核加载后调用的第一个函数
    cli
    mov [temp_mboot_ptr],ebx
    mov esp,INIT_STACK_TOP
    and esp, 0xFFFFFFF0  ;16字节对齐
    call set_page
    mov ebx,[temp_mboot_ptr]
    mov eax,temp_dir_table
    mov cr3,eax
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax
    jmp boot_start_after_set_paging
set_page:
    mov eax,8191      ;2*4096-1
    .clear_mem:             ;重置页表以及页目录表的内存空间
    mov  byte[temp_dir_table+eax],0
    dec eax
    jnz .clear_mem
    mov byte[temp_dir_table],0

    .create_pde:            ;创建页目录表
    mov eax,temp_page_table       
    or eax,PG_US_S|PG_RW_W|PG_P     ;eax存放了一张页表信息    也就是一条页目录项
    mov [temp_dir_table+0xc00],eax      ;构建页目录项
    mov [temp_dir_table],eax    ;起始位置的4MB映射到对应的4MB（虚拟地址与物理地址重合）
    
    mov eax,1023
    .create_pte:              ;创建一条页目录项对应的页表
    mov ebx,eax
    sal ebx,12
    and ebx,0xFFFFF000
    or ebx,PG_US_U|PG_RW_W|PG_P
    mov [temp_page_table+eax*4],ebx
    dec eax
    jnz .create_pte
    mov ebx,0
    or ebx,PG_US_S|PG_RW_W|PG_P
    mov [temp_page_table],ebx
    ret
section .init.data align=4096            ;依据我的测试结果来看   section是默认4096对齐的
;需要4K对齐
temp_dir_table:
 resb 4096
temp_page_table:
resb 4096          ;只能映射前4M的内核空间
init_stack:
resb 1024      ;1024B暂用栈
INIT_STACK_TOP equ $-1
[BITS 32]   ;由于GRUB在加载内核前进入保护模式，所以要32位编译   
section .text    
[GLOBAL mboot_ptr]  
[EXTERN kern_entry]
   GDT_BASE:   dd    0x00000000 
           	   dd    0x00000000

   CODE_DESC:  dd    0x0000FFFF 
               dd    DESC_CODE_HIGH4

   DATA_STACK_DESC:  dd    0x0000FFFF
                     dd    DESC_DATA_HIGH4

   VIDEO_DESC: dd    0x80000007        ; limit=(0xbffff-0xb8000)/4k=0x7
               dd    DESC_VIDEO_HIGH4  ; 此时dpl为0

   GDT_SIZE   equ   $ - GDT_BASE
   GDT_LIMIT   equ   GDT_SIZE - 1 
   times 30 dq 0                     ; 此处预留30个描述符的空位
   SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0     ; 相当于(CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
   SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0     ; 同上
   SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0    ; 同上 

   total_mem_bytes dd 0                  
   ;以下是定义gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
   gdt_ptr  dw  GDT_LIMIT 
        	dd  GDT_BASE
;boot开始！
boot_start_after_set_paging:        ;此处修改了函数名     在设置好页表后调用此函数
    cli                        ;关闭外中断
    mov ebx,[temp_mboot_ptr]     ;此处将暂存的mboot信息取出    但是一定要注意：必须要前4MB的物理-虚拟内存映射才能够使用
    mov [mboot_ptr], ebx ; GRUB加载内核后会将mutiboot信息地址存放在ebx中
    ;-----------------   准备进入保护模式   -------------------
;1 打开A20
;2 加载gdt
;3 将cr0的pe位置1
   ;-----------------  打开A20  ----------------
    in al,0x92
    or al,0000_0010B
    out 0x92,al
   ;-----------------  加载GDT  ----------------
    lgdt [gdt_ptr]
   ;-----------------  cr0第0位置1  ----------------
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp dword SELECTOR_CODE:far_jmp_target      ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，

;初始化段寄存器以及栈结构
    far_jmp_target:
    mov ax,SELECTOR_DATA
    mov ss,ax
    mov ds,ax
  	mov ax,SELECTOR_VIDEO
  	mov gs,ax
    mov esp, STACK_TOP      
    and esp, 0xFFFFFFF0  ;16字节对齐
    mov ebp, 0           
;进入内核主函数    
    call kern_entry                    
    jmp dword $          ;防止意外退出内核

section .data
mboot_ptr:        
    dd 0x0        

section .bss             ; 未初始化的数据段从这里开始    注意bss段是不占用存储器空间的，是在程序加载后才在内存中分配的
stack:
    resb 0x80000        ; 512KB的内核栈 (应该够了吧,不够自己改)
STACK_TOP equ $-1      