;内核线程模块的汇编函数文件
[bits 32]
[GLOBAL get_esp]
get_esp:
	mov eax,esp
	ret
[GLOBAL get_eflags]
get_eflags:
	pushf
	pop eax
	ret
[GLOBAL switch_to]
switch_to:
	;保存上下文
	mov eax,[esp+4]     ;第一个参数 
	mov [eax],ebp
	mov [eax+4],ebx
	mov [eax+8],ecx
	mov [eax+12],edx
	mov [eax+16],esi
	mov [eax+20],edi
	mov [eax+28],esp
	push ebx
	mov ebx,eax
	pushf
	pop eax
	mov [ebx+24],eax
	mov eax,ebx
	pop ebx

	;加载上下文
	mov eax,[esp+8]      ;第二个参数
	mov esp,[eax+28]    ;切换了esp    导致ret指令控制流转移
	mov ebp,[eax]
	mov ebx,[eax+4]
	mov ecx,[eax+8]
	mov edx,[eax+12]
	mov esi,[eax+16]
	mov edi,[eax+20]
	add eax,24
	push dword [eax] ;eflags
	popf	

	;由于8259a设置的手动模式 所以必须给主片与从片发送信号 否则8259a会暂停
	;这个bug找了一下午才找到 顺便吐槽下 内核级的代码debug太难了(GDB在多线程与汇编级会失效 只有print调试法) 
	mov al,0x20         
	out 0xA0,al
	out 0x20,al
	
	ret                  ;执行下一个函数