#include "led.h"
#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "spi.h"
#include "port.h"
#include "instance.h"
#include "deca_types.h"
#include "deca_spi.h"
#include "device_info.h" 
#include "stmflash.h"
#include "dma.h"
#include "deca_regs.h"
#include "hw_config.h"
#include "stm32f10x.h"	
#include "stm32f10x_tim.h"
#include "W5500.h"			
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436
u8 SendBuff[130];
u8 USB_RxBuff[30];

static uint8 tx_msg[] = {0x41, 0x88, 0, 0xca, 0xde, 0x01, 0xb1, 0x01, 0xa1, 0, 0, 0,0,0};
#define UUS_TO_DWT_TIME 65536	
#define Gateway_DDRL 0x01       /* ���ض̵�ַ��λ */
#define Gateway_DDRH 0xB1       /* ���ض̵�ַ��λ */
#define PANID 0xde01  
#define PANID_DDRL 0x01      
#define PANID_DDRH 0xde 
static dwt_config_t config = {
    2,               /* Channel number. */
    DWT_PRF_64M,     /* Pulse repetition frequency. */
    DWT_PLEN_1024,   /* Preamble length. Used in TX only. */
    DWT_PAC32,       /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    1,               /* 0 to use standard SFD, 1 to use non-standard SFD. */
    DWT_BR_110K,     /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    1,
	  1057 
};
 struct base_task_t
{
  TaskHandle_t          thread;
  QueueHandle_t        evtq;
  
  bool             isactive;
};
 struct base_task_t base_task;

uint64_t timer3_tick_ms=0; //Timer3��ʱ����������(ms)
unsigned int W5500_Send_Delay_Counter=0; //W5500������ʱ��������(ms)
void Delay(unsigned int d);			//��ʱ����(ms)


/*******************************************************************************
* ������  : W5500_Initialization
* ����    : W5500��ʼ������
* ����    : ��
* ���    : ��
* ����ֵ  : ��
* ˵��    : ��
*******************************************************************************/
void W5500_Initialization(void)
{
	W5500_Init();		//��ʼ��W5500�Ĵ�������
	Detect_Gateway();	//������ط����� 
	Socket_Init(0);		//ָ��Socket(0~7)��ʼ��,��ʼ���˿�0
}

/*******************************************************************************
* ������  : Load_Net_Parameters
* ����    : װ���������
* ����    : ��
* ���    : ��
* ����ֵ  : ��
* ˵��    : ���ء����롢�����ַ������IP��ַ���˿ںš�Ŀ��IP��ַ��Ŀ�Ķ˿ںš��˿ڹ���ģʽ
*******************************************************************************/
void Load_Net_Parameters(void)
{
	Gateway_IP[0] = 192;//�������ز���
	Gateway_IP[1] = 168;
	Gateway_IP[2] = 2;
	Gateway_IP[3] = 1;

	Sub_Mask[0]=255;//������������
	Sub_Mask[1]=255;
	Sub_Mask[2]=255;
	Sub_Mask[3]=0;

	Phy_Addr[0]=0x0c;//���������ַ
	Phy_Addr[1]=0x29;
	Phy_Addr[2]=0xab;
	Phy_Addr[3]=0x7c;
	Phy_Addr[4]=0x00;
	Phy_Addr[5]=0x01;

	IP_Addr[0]=192;//���ر���IP��ַ
	IP_Addr[1]=168;
	IP_Addr[2]=2;
	IP_Addr[3]=5;

	S0_Port[0] = 0x13;//���ض˿�0�Ķ˿ں�5000 
	S0_Port[1] = 0x88;

//	S0_DIP[0]=192;//���ض˿�0��Ŀ��IP��ַ
//	S0_DIP[1]=168;
//	S0_DIP[2]=1;
//	S0_DIP[3]=190;
//	
//	S0_DPort[0] = 0x17;//���ض˿�0��Ŀ�Ķ˿ں�6000
//	S0_DPort[1] = 0x70;

	UDP_DIPR[0] = 192;	//UDP(�㲥)ģʽ,Ŀ������IP��ַ
	UDP_DIPR[1] = 168;
	UDP_DIPR[2] = 2;
	UDP_DIPR[3] = 52;
//
	UDP_DPORT[0] = 0x1F;	//UDP(�㲥)ģʽ,Ŀ�������˿ں�
	UDP_DPORT[1] = 0x90;

	S0_Mode=UDP_MODE;//���ض˿�0�Ĺ���ģʽ,UDPģʽ
}

/*******************************************************************************
* ������  : W5500_Socket_Set
* ����    : W5500�˿ڳ�ʼ������
* ����    : ��
* ���    : ��
* ����ֵ  : ��
* ˵��    : �ֱ�����4���˿�,���ݶ˿ڹ���ģʽ,���˿�����TCP��������TCP�ͻ��˻�UDPģʽ.
*			�Ӷ˿�״̬�ֽ�Socket_State�����ж϶˿ڵĹ������
*******************************************************************************/
void W5500_Socket_Set(void)
{
	if(S0_State==0)//�˿�0��ʼ������
	{
		if(S0_Mode==TCP_SERVER)//TCP������ģʽ 
		{
			if(Socket_Listen(0)==TRUE)
				S0_State=S_INIT;
			else
				S0_State=0;
		}
		else if(S0_Mode==TCP_CLIENT)//TCP�ͻ���ģʽ 
		{
			if(Socket_Connect(0)==TRUE)
				S0_State=S_INIT;
			else
				S0_State=0;
		}
		else//UDPģʽ 
		{
			if(Socket_UDP(0)==TRUE)
				S0_State=S_INIT|S_CONN;
			else
				S0_State=0;
		}
	}
}

/*******************************************************************************
* ������  : Process_Socket_Data
* ����    : W5500���ղ����ͽ��յ�������
* ����    : s:�˿ں�
* ���    : ��
* ����ֵ  : ��
* ˵��    : �������ȵ���S_rx_process()��W5500�Ķ˿ڽ������ݻ�������ȡ����,
*			Ȼ�󽫶�ȡ�����ݴ�Rx_Buffer������Temp_Buffer���������д���
*			������ϣ������ݴ�Temp_Buffer������Tx_Buffer������������S_tx_process()
*			�������ݡ�
*******************************************************************************/
void Process_Socket_Data(SOCKET s)
{
	unsigned short size;
	size=Read_SOCK_Data_Buffer(s, Rx_Buffer);
	UDP_DIPR[0] = Rx_Buffer[0];
	UDP_DIPR[1] = Rx_Buffer[1];
	UDP_DIPR[2] = Rx_Buffer[2];
	UDP_DIPR[3] = Rx_Buffer[3];

	UDP_DPORT[0] = Rx_Buffer[4];
	UDP_DPORT[1] = Rx_Buffer[5];
	memcpy(Tx_Buffer, Rx_Buffer+8, size-8);			
	Write_SOCK_Data_Buffer(s, Tx_Buffer, size);
}
void Timer3_Init(u16 arr,u16 psc)
{
     TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
     NVIC_InitTypeDef NVIC_InitStructure;
 
     RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
 
     TIM_TimeBaseStructure.TIM_Period = (arr - 1); 
     TIM_TimeBaseStructure.TIM_Prescaler =(psc-1); 
     TIM_TimeBaseStructure.TIM_ClockDivision = 0; 
     TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  
     TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure); 
  
      
     TIM_ITConfig(  
         TIM3, 
         TIM_IT_Update  |  
         TIM_IT_Trigger,  
         ENABLE  
         );
      
     NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;  //TIM3??
     NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  
     NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;  
     NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
     NVIC_Init(&NVIC_InitStructure);  
 
     TIM_Cmd(TIM3, ENABLE);  
                              
 }
 
 void TIM3_IRQHandler(void)   
 {
     if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) 
         {
            TIM_ClearITPendingBit(TIM3, TIM_IT_Update);  
            timer3_tick_ms += 10;
         }
}
void vApplicationIdleHook( void )
{
}

void vApplicationTickHook(void)
{
}
void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
  __nop;
}
void  base_task_thead(void * arg)
{
    u8 eui64[2];
	int i;
	static uint8 rx_buffer[127];
	static uint32 status_reg = 0;
	static uint16 frame_len = 0;

	u8 message[15];
	uint8 t1[5],t2[5];
	u8 tag_f;
	int num=0;             //������Ϣ������ ������ʱ

	base_task.isactive = 1;
	while (base_task.isactive)
    {
		SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);
		reset_DW1000(); 	
		port_SPIx_clear_chip_select();
		delay_ms(110);
		port_SPIx_set_chip_select();
		if (dwt_initialise(DWT_LOADUCODE | DWT_LOADLDOTUNE | DWT_LOADTXCONFIG | DWT_LOADANTDLY| DWT_LOADXTALTRIM)  == DWT_ERROR)
		{	
			led_on(LED_ALL);
			i=10;
			while (i--)
			{
				led_on(LED_ALL);
				delay_ms(200);
				led_off(LED_ALL);
				delay_ms(200);
			};
		}
 	    dwt_setleds(3) ; 
        dwt_configure(&config,DWT_LOADXTALTRIM);	
        eui64[0]=Gateway_DDRL;         //�������ص�ַ
	    eui64[1]=Gateway_DDRH;
	    dwt_seteui(eui64);
        dwt_setpanid(PANID);
        tx_msg[4]=PANID_DDRH;
        tx_msg[3]=PANID_DDRL;
        tx_msg[5]=0xff;
        tx_msg[6]=0xff;
        tx_msg[7]=Gateway_DDRL;
        tx_msg[8]=Gateway_DDRH;

		led_on(LED_ALL);
        port_EnableEXT_IRQ();
        dwt_setrxantennadelay(0);
        dwt_settxantennadelay(0);	
        //    dwt_setrxtimeout(60000);	
        W5500_Socket_Set();//W5500�˿ڳ�ʼ������

        W5500_Interrupt_Process();//W5500�жϴ��������

        if((S0_Data & S_RECEIVE) == S_RECEIVE)//���Socket0���յ�����
        {
            S0_Data&=~S_RECEIVE;
            Process_Socket_Data(0);//W5500���ղ����ͽ��յ�������
        }
        for (i = 0 ; i < 127; i++ )
        {
            rx_buffer[i] = 0;
        }
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
        led_on(LED_ALL);
        while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG | (SYS_STATUS_RXPHE | SYS_STATUS_RXFCE | SYS_STATUS_RXRFSL | SYS_STATUS_RXSFDTO \
                                                        | SYS_STATUS_AFFREJ | SYS_STATUS_LDEERR))))
        { };
        if (status_reg & SYS_STATUS_RXFCG)
		{
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
            if (frame_len <= 127)
            {
                dwt_readrxdata(rx_buffer, frame_len, 0);
            }
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
            dwt_readrxtimestamp(t1);
            if(rx_buffer[4]==0xff&&rx_buffer[5]==0xff)  //������ϢΪ����֡
            {
                message[1]=Gateway_DDRL;
                message[0]=Gateway_DDRH;
                message[2]=0xad;
                message[3]=rx_buffer[1];
                message[4]=rx_buffer[6];
                message[5]=rx_buffer[7];
                for(i=6;i<11;i++)
                {
                    message[i]=t1[i-6];
                }
                message[11]=0xff;
                num++;
                Write_SOCK_Data_Buffer(0, message, 12);
//						USB_TxWrite(message,12);
            }
			else if(rx_buffer[4]==0xee&&rx_buffer[5]==0xee)
            {
				message[1]=Gateway_DDRL;
				message[0]=Gateway_DDRH;
				message[2]=0xab;
				message[3]=rx_buffer[1];
				message[4]=rx_buffer[2];
                message[5]=rx_buffer[3];
                for(i=6;i<11;i++)
				{
                    message[i]=t1[i-6];
				}
				message[11]=0xff;
				USB_TxWrite(message,12);
				Write_SOCK_Data_Buffer(0, message, 12);
				num++;       //������ϢΪ��λ֡ʱ����
				tag_f=rx_buffer[8];   //��λ��ǩ����Ƶ��
			}
	 
				 if(num%2==1)
				 {
//				    delay_ms(50);
//					dwt_readsystime(t2);	
//					for(i=9;i<14;i++)
//					{
//					    tx_msg[i]=t2[i-9];
//				 	}	 
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
                    dwt_writetxfctrl(sizeof(tx_msg), 0); /* Zero offset in TX buffer, no ranging. */            
                    dwt_starttx(DWT_START_TX_IMMEDIATE); 
                    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
                    { };
                    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
                    dwt_readtxtimestamp(t2);
                    message[1]=Gateway_DDRL;
                    message[2]=Gateway_DDRH;
                    message[2]=0xaa;
                    message[3]=tx_msg[2];
                    for(i=4;i<9;i++)
                    {
                         message[i]=t2[i-4];
                    }
                    message[9]=0xff;
                    tx_msg[2]++;
                    Write_SOCK_Data_Buffer(0,message,10);
//					USB_TxWrite(tx_msg,15);				
				 }	
			}					
            else
            {
                /* Clear RX error events in the DW1000 status register. */
                dwt_write32bitreg(SYS_STATUS_ID, (SYS_STATUS_RXPHE | SYS_STATUS_RXFCE | SYS_STATUS_RXRFSL | SYS_STATUS_RXSFDTO \
                                                                 | SYS_STATUS_AFFREJ | SYS_STATUS_LDEERR));
                num++;
            }
	}

}
uint32_t base_task_start ()
{
  if (base_task.isactive)
    return 1;

  // Start execution.
  if (pdPASS != xTaskCreate (base_task_thead, "BASE", 256, &base_task, 4, &base_task.thread))
  {
    return 1;
  }
  return 0;
}
	
int main(void)
{ 
    peripherals_init();
	delay_init();
	uart_init(115200);	 //���ڳ�ʼ��Ϊ115200 	
    GPIO_Configuration();//��ʼ����LED���ӵ�Ӳ���ӿ�
    SPI_Configuration();
    Timer3_Init(100,7200);  //10ms
    delay_ms(1000);
    Flash_Configuration();
    delay_ms(200);
    USB_Config();
    MYDMA_Config(DMA1_Channel4,(u32)&USART1->DR,(u32)SendBuff,130);//DMA1ͨ��4,����Ϊ����1,�洢��ΪSendBuff,����130.  
    USART_DMACmd(USART1,USART_DMAReq_Tx,ENABLE); //ʹ�ܴ���1��DMA����  
    MYDMA_Enable(DMA1_Channel4);//��ʼһ��DMA���䣡		

    SPI2_Configuration();		//W5500 SPI��ʼ������(STM32 SPI1)
    W5500_GPIO_Configuration();	//W5500 GPIO��ʼ������	
    Load_Net_Parameters();		//װ���������	
    W5500_Hardware_Reset();		//Ӳ����λW5500
    W5500_Initialization();		//W5500��ʼ������	
    base_task_start();
    vTaskStartScheduler();
    while (1)
    {   
    }
		
}	
	
/*******************************************************************************
* ������  : Delay
* ����    : ��ʱ����(ms)
* ����    : d:��ʱϵ������λΪ����
* ���    : ��
* ����    : �� 
* ˵��    : ��ʱ������Timer2��ʱ��������1����ļ�����ʵ�ֵ�
*******************************************************************************/
//void Delay(unsigned int d)
//{
//	unsigned char tt=0;
//	Timer2_Counter=0; 
//  while(1)
//	{
//		if (Timer2_Counter>=d ) 
//{
//break;
//}
//	}
//	tt=0;
//}

void Delay(unsigned int d)
{
	delay_ms(d);
	
}


