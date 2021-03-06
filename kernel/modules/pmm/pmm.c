/*
/kernel/init/pmm.c
by 不吃香菜的大头怪
2020/2/14
内核物理页管理  伙伴算法＋单页缓冲区算法
*/
//注意 此模块只适用于至少8M可用空间的机器 因为必须需要至少2048个页来填充分配主链表
#include "pmm.h"
#include "types.h"
#include "printk.h"
#include "boot_struct.h"
#define ERRO_POP_BLOCK 0xFFFFFFFF    //无法寻找页的返回
extern multiboot_t * mboot_ptr;      //GRUB启动结构体指针 定义在boot.s中 
extern uint8_t kern_start[];
extern uint8_t timer;
void clear_screen();

extern uint8_t kern_end[];
static uint32_t pmm_page_start;
static uint32_t pmm_page_end;
static uint32_t pmm_max_page_no;    //最大块编号 不能达到的上限(或者我叫做开区间？)
static uint32_t singel_page_first_no;
//用于block计数的存放
uint32_t block_count_array[12]={0};
//page_array用来存放页描述结构体的数组 使用动态分配的方式可以节约200KB软盘空间(对我1.44M的可怜空间来说 这已经很多了)
//注意：不要放在0x0地址 不然会与NULL冲突（他喵的 这bug我找了几个小时）并且选择一个合适的对齐 便于
//由于page_array放置地址问题 最大支持不到4GB 过大的数组会扩展到破坏内核空间(可修改存放在1M以上)
pm_page_t *page_array = (pm_page_t*)0xC0000100;       
pm_multi_link_t * MULTI_LINK;   //用大写来表示很重要 并且定义为结构体指针，用->更加美观了～
pm_multi_link_t multi_link_struct={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
pm_page_t * SINGLE_LINK = NULL ;
//获取page编号对应的addr
static uint32_t pmm_page_no_to_addr(uint32_t page_no){
	return (page_no<<12)+pmm_page_start;
}

//addr转为page_no
static uint32_t addr_to_pmm_page_no(uint32_t addr){
	return (addr-pmm_page_start)>>12;
}

//将 page_c_t枚举类型转化为对应的块大小 如 (page_c_t)_256——> (uint32_t)256
static uint32_t c_to_uint32(page_c_t ph){
	uint32_t re = 1;
	re = re<<ph;
	return re;
}

static uint32_t get_partner_page_no(uint32_t page_no,page_c_t type){
	//--|--|--|--|--|--| 如图 必须整数倍或者0
	uint32_t v1 = c_to_uint32(type);
	uint32_t v2 = 2*v1;
	if((page_no - v1)%v2 == 0)
		return page_no-v1;
	else
		return page_no+v1;
}


//向链表添加块（用于初始化链表以及free后添加块）
//此处可以使用##连接宏（但是我偏不）
static void append_block(int page_no,page_c_t c){
	page_array[page_no].next = NULL ;     //一定要设置 新加入块的下个指针为NULL
	pm_page_t * header;
	switch(c){
		case _1:
			header = MULTI_LINK ->_1;
			if(header==NULL)
				MULTI_LINK ->_1 = &(page_array[page_no]);
			break;
		case _2:
			header = MULTI_LINK ->_2;
			if(header==NULL)
				MULTI_LINK ->_2 = &(page_array[page_no]);
			break;
		case _4:
			header = MULTI_LINK ->_4;
			if(header==NULL)
				MULTI_LINK ->_4 = &(page_array[page_no]);
			break;
		case _8:
			header = MULTI_LINK ->_8;
			if(header==NULL)
				MULTI_LINK ->_8 = &(page_array[page_no]);
			break;
		case _16:
			header = MULTI_LINK ->_16;
			if(header==NULL)
				MULTI_LINK ->_16 = &(page_array[page_no]);
			break;
		case _32:
			header = MULTI_LINK ->_32;
			if(header==NULL)
				MULTI_LINK ->_32 = &(page_array[page_no]);
			break;
		case _64:
			header = MULTI_LINK ->_64;
			if(header==NULL)
				MULTI_LINK ->_64 = &(page_array[page_no]);
			break;
		case _128:
			header = MULTI_LINK ->_128;
			if(header==NULL)
				MULTI_LINK ->_128 = &(page_array[page_no]);
			break;
		case _256:
			header = MULTI_LINK ->_256;
			if(header==NULL)
				MULTI_LINK ->_256 = &(page_array[page_no]);
			break;
		case _512:
			header = MULTI_LINK ->_512;
			if(header==NULL)
				MULTI_LINK ->_512 = &(page_array[page_no]);
			break;
		case _1024:
			header = MULTI_LINK ->_1024;
			if(header==NULL)
				MULTI_LINK ->_1024 = &(page_array[page_no]);
			break;
		case _erro:
			break;
	}
	if(header!=NULL){
		pm_page_t * probe = header;
		for(;(probe->next)!=NULL;probe = probe->next)
			;
		(probe -> next)=&(page_array[page_no]);
	}
}

//从链表中取出一个块 链表为空返回ERRO_POP_BLOCK
static uint32_t pop_block(page_c_t c){
	pm_page_t * header;
	switch(c){
		case _1:
			header = MULTI_LINK ->_1;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_1 =NULL;
				return header->page_no;
			}
			break;
		case _2:
			header = MULTI_LINK ->_2;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_2 = NULL;
				return header->page_no;
			}
			break;
		case _4:
			header = MULTI_LINK ->_4;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_4 = NULL;
				return header->page_no;
			}
			break;
		case _8:
			header = MULTI_LINK ->_8;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_8 = NULL;
				return header->page_no;
			}
			break;
		case _16:
			header = MULTI_LINK ->_16;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_16 = NULL;
				return header->page_no;
			}
			break;
		case _32:
			header = MULTI_LINK ->_32;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_32 = NULL;
				return header->page_no;
			}
			break;
		case _64:
			header = MULTI_LINK ->_64;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_64 = NULL;
				return header->page_no;
			}
			break;
		case _128:
			header = MULTI_LINK ->_128;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_128 = NULL;
				return header->page_no;
			}
			break;
		case _256:
			header = MULTI_LINK ->_256;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_256 = NULL;
				return header->page_no;
			}
			break;
		case _512:
			header = MULTI_LINK ->_512;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_512 = NULL;
				return header->page_no;
			}
			break;
		case _1024:
			header = MULTI_LINK ->_1024;
			if(header==NULL)
				return ERRO_POP_BLOCK;
			if(header->next==NULL){
				MULTI_LINK ->_1024 = NULL;
				return header->page_no;
			}
			break;
		case _erro:
			return ERRO_POP_BLOCK; 
			break;
	}
	pm_page_t * probe = header;
	for(;probe->next->next!=NULL;probe = probe->next)
		;
	uint32_t return_page_no = probe->next->page_no;
	probe->next=NULL;
	return return_page_no;
}
 
//free合并块使用函数
//如果链表中没有 返回ERRO_POP_BLOCK 否则返回对应page_no
static uint32_t find_and_pop_block(uint32_t target_page_no,page_c_t ph){
	pm_page_t * header;
	switch(ph){
		case _1:
			header = MULTI_LINK -> _1;
			break;
		case _2:
			header = MULTI_LINK -> _2;
			break;
		case _4:
			header = MULTI_LINK -> _4;
			break;
		case _8:
			header = MULTI_LINK -> _8;
			break;
		case _16:
			header = MULTI_LINK -> _16;
			break;
		case _32:
			header = MULTI_LINK -> _32;
			break;
		case _64:
			header = MULTI_LINK -> _64;
			break;
		case _128:
			header = MULTI_LINK -> _128;
			break;
		case _256:
			header = MULTI_LINK -> _256;
			break;
		case _512:
			header = MULTI_LINK -> _512;
			break;
		case _1024:
			header = MULTI_LINK -> _1024;
			break;
		case _erro:
			return ERRO_POP_BLOCK;
	}
	if(header == NULL)
		return ERRO_POP_BLOCK;
	if(header->next==NULL){
		if(header->page_no==target_page_no){
			pop_block(ph);
			return target_page_no;
		}
	}
	pm_page_t * probe = header ;
	//
	//
	//
	//
	//
	//
	for(; probe->next!=NULL;probe=probe->next){
		if(probe->next->page_no == target_page_no){
			probe->next = probe->next->next;
			return target_page_no;
		}
	}
	if(probe->page_no == target_page_no){
		pop_block(ph);
		return target_page_no;
	}
	return ERRO_POP_BLOCK;
}

//从multi_boot结构体中取出需要管理的地址空间大小 
static uint32_t get_max_pm_addr(){          //qemu默认为128M
	uint32_t max_addr=0;
	uint32_t p = (uint32_t)mboot_ptr;
	for(pm_entry_t * pm_entry_cur = mboot_ptr->mmap_addr;pm_entry_cur<mboot_ptr->mmap_addr+mboot_ptr->mmap_length;pm_entry_cur++){
		printk("[INFO][PMM]physic_mem_block:0x%h-0x%h-0x%h-%d\n",pm_entry_cur->base_addr_low,pm_entry_cur->length_low,pm_entry_cur->base_addr_low+pm_entry_cur->length_low,pm_entry_cur->type);
		if(pm_entry_cur->type==1&&max_addr<pm_entry_cur->base_addr_low+pm_entry_cur->length_low)
			max_addr=pm_entry_cur->base_addr_low+pm_entry_cur->length_low;		
	}
	return max_addr;
}

//初始化页描述结构体以及装载链表
static void pmm_page_init(){  //初始化链表结构体并且填充链表
	MULTI_LINK=&multi_link_struct;
	MULTI_LINK->_1=NULL;
	MULTI_LINK->_2=NULL;
	MULTI_LINK->_4=NULL;
	MULTI_LINK->_8=NULL;
	MULTI_LINK->_16=NULL;
	MULTI_LINK->_32=NULL;
	MULTI_LINK->_64=NULL;
	MULTI_LINK->_128=NULL;
	MULTI_LINK->_256=NULL;
	MULTI_LINK->_512=NULL;
	MULTI_LINK->_1024=NULL;
	//我们将要余出至多1023页来作为单页分配的一个缓冲区 这个缓冲区是与伙伴算法独立的 便于快速分配单页
	for(int i=0;i<pmm_max_page_no;i++){
		page_array[i].page_no = i;
		page_array[i].state = 1;
		page_array[i].next = NULL;
	}
	//装载链表
	append_block(0,_1);    //1页链表
	append_block(1,_1);

	append_block(2,_2);    //2页链表

	append_block(4,_4);

	append_block(8,_8);

	append_block(16,_16);

	append_block(32,_32);

	append_block(64,_64);

	append_block(128,_128);

	append_block(256,_256);

	append_block(512,_512);

	append_block(1024,_1024);


	int temp_page_no=2048;
	for(;temp_page_no<(pmm_max_page_no-1024);temp_page_no+=1024){
		append_block(temp_page_no,_1024);
	}
	singel_page_first_no = temp_page_no;   //将第一个单页缓冲区的编号存放好

	printk("[INFO][PMM]single_page_count:%d\n",pmm_max_page_no-temp_page_no);
	pm_page_t * temp_single_probe = NULL;
	for(;temp_page_no<pmm_max_page_no;temp_page_no++){
		if(SINGLE_LINK == NULL){
			SINGLE_LINK=&(page_array[temp_page_no]);
			temp_single_probe = SINGLE_LINK ;
		}
		else{
			temp_single_probe->next = &(page_array[temp_page_no]);
			temp_single_probe = temp_single_probe -> next;
		}
	}
}







static uint32_t alloc_helper(page_c_t target_ph,page_c_t pop_ph,uint32_t pop_page_no){
	for(;pop_ph!=target_ph;pop_ph--){
		uint32_t append_page_no = pop_page_no+(c_to_uint32(pop_ph)/2);
		append_block(append_page_no,pop_page_no-1);
	}
	return pop_page_no;
}



//伙伴算法多页分配
pm_alloc_t pmm_alloc_pages(uint32_t page_count){
	pm_alloc_t return_struct = {0,_erro,0}; 
	if (page_count>1024||page_count==0)
		return return_struct;    //分配失败
	//来点骚操作
	//获取需要分配的页数
	uint32_t page_count_probe=page_count;
	uint32_t counter1 = 0;
	for(;page_count_probe!=0 ;counter1++,page_count_probe=page_count_probe>>1)
		;
	uint32_t counter2= counter1-1;
	if(page_count==(1<<counter2))
		counter1--;
	//此时的counter1即为pm_c_t枚举的对应值
	page_c_t ph = counter1;
	page_c_t origin_ph = ph;    //存放原始ph
	uint32_t pop_page_no = ERRO_POP_BLOCK;
	for(;ph<_erro;ph++){
		pop_page_no = pop_block(ph);
		if(pop_page_no!=ERRO_POP_BLOCK)
			break;
	}
	if(pop_page_no==ERRO_POP_BLOCK){
		return return_struct;    //分配失败
	}
	else{
		uint32_t target_page_no = alloc_helper(origin_ph,ph,pop_page_no);
		return_struct.addr = pmm_page_no_to_addr(target_page_no);
		return_struct.state = 1;
		return_struct.size = origin_ph;
		return return_struct;
	}
}

//缓冲区及伙伴算法单页分配
//当缓冲区没有页的时候会调用伙伴算法分配一页
pm_alloc_t pmm_alloc_one_page(){
	pm_alloc_t return_struct = {0,_erro,0};
	if(SINGLE_LINK!=NULL){
		pm_page_t * header = SINGLE_LINK;
		if(header->next==NULL){
			SINGLE_LINK = NULL;
			return_struct.state = 1;
			return_struct.size = _1;
			return_struct.addr =pmm_page_no_to_addr(header->page_no);
			return return_struct;
		}
		else{
			pm_page_t * probe = header;
			for(;probe->next->next!=NULL;probe=probe->next){
				//printk("%d\n",probe->page_no);
				//if(probe->page_no==0){
				//	while(1);
				//}
			}
			return_struct.state = 1;
			return_struct.size = _1;
			return_struct.addr =pmm_page_no_to_addr(probe->next->page_no);
			probe->next = NULL;
		}
		return return_struct;
	}
	else{
		return pmm_alloc_pages(1);
	}
}

static void free_helper(uint32_t page_no,page_c_t size){
	printk("free size : %d\n",c_to_uint32(size));
	uint32_t partner_page_no = get_partner_page_no(page_no,size);
	if(find_and_pop_block(partner_page_no,size)!=ERRO_POP_BLOCK){
		//合并
		free_helper(partner_page_no<page_no?partner_page_no:page_no,size+1);
	}
	else{
		append_block(page_no,size);
	}
}

//free页 返回bool型（定义在typs.h中） True-成功free False-失败
bool pmm_free_page(pm_alloc_t block_disc){
	//检查页是否在页编号范围内
	uint32_t page_no = addr_to_pmm_page_no(block_disc.addr);
	if(page_no<pmm_max_page_no){
		if(page_no<singel_page_first_no)
			free_helper(page_no,block_disc.size);
		else{
			pm_page_t *probe = SINGLE_LINK;
			if(probe==NULL){
				SINGLE_LINK=&page_array[page_no];
			}
			else{
				for(;probe->next!=NULL;probe=probe->next)
					;
				probe->next = &page_array[page_no];
			}
		}
		return True;
	}
	else
		return False;
}

static uint32_t counte_helper(pm_page_t * probe){
	uint32_t counter = 0;
	for(;probe!=NULL;probe = probe->next)
		counter++;
	return counter;
}

uint32_t * get_block_count(){
	block_count_array[_1]=counte_helper(MULTI_LINK->_1);
	block_count_array[_2]=counte_helper(MULTI_LINK->_2);
	block_count_array[_4]=counte_helper(MULTI_LINK->_4);
	block_count_array[_8]=counte_helper(MULTI_LINK->_8);
	block_count_array[_16]=counte_helper(MULTI_LINK->_16);
	block_count_array[_32]=counte_helper(MULTI_LINK->_32);
	block_count_array[_64]=counte_helper(MULTI_LINK->_64);
	block_count_array[_128]=counte_helper(MULTI_LINK->_128);
	block_count_array[_256]=counte_helper(MULTI_LINK->_256);
	block_count_array[_512]=counte_helper(MULTI_LINK->_512);
	block_count_array[_1024]=counte_helper(MULTI_LINK->_1024);
	block_count_array[_erro]=0;
	return block_count_array;
}

uint32_t get_single_count(){
	pm_page_t * probe =SINGLE_LINK;
	uint32_t counter = 0;
	for(;probe!=NULL;probe = probe->next)
		counter++;
	return counter;	
}

//打印块使用情况的函数 
//懒得挨个写 用宏来解决
// #表示字符串化 ##表示连接生成符号操作
#define MACRO_PMM_1(number) printk("_"#number":%d blocks\n",block_count_array[_##number])
void pmm_show_page_count(){
	get_block_count();
	printk("Partner:\n");
	MACRO_PMM_1(1);
	MACRO_PMM_1(2);
	MACRO_PMM_1(4);
	MACRO_PMM_1(8);
	MACRO_PMM_1(16);
	MACRO_PMM_1(32);
	MACRO_PMM_1(64);
	MACRO_PMM_1(128);
	MACRO_PMM_1(256);
	MACRO_PMM_1(512);
	MACRO_PMM_1(1024);
	printk("singel buffer have:%d pages!\n",get_single_count());
}
//取消此宏定义 将宏定义范围限制在此函数中
#undef MACRO_PMM_1(number)

//为内核entry使用的pmm管理模块初始化函数
void pmm_init(){
	printk("[INFO][PMM]kern_physic_start:0x%h\n",kern_start);
	printk("[INFO][PMM]kern_physic_end:0x%h\n",kern_end);
	//一定要注意 由于分页必须4k对齐 所以此处的物理页管理必须与虚拟页相同 都要4K对齐
	pmm_page_start = ((((uint32_t)kern_end >> 12))+1)<<12;
	pmm_page_end = (((get_max_pm_addr() >> 12)))<<12;
	pmm_max_page_no = ((pmm_page_end - pmm_page_start)>>12);
	printk("[INFO][PMM]pmm_start:0x%h\n",pmm_page_start);
	printk("[INFO][PMM]pmm_end:0x%h\n",pmm_page_end);
	printk("[INFO][PMM]physic_page_count:%d\n",pmm_max_page_no+1);
	printk("[INFO][PMM]first_page_physic_addr:0x%h\n",pmm_page_no_to_addr(32000));
	pmm_page_init();
}