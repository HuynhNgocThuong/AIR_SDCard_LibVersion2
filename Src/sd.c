
#include "sd.h"
//--------------------------------------------------
// Definitions for MMC/SDC command
#define CMD0 (0x40+0) // GO_IDLE_STATE
#define CMD1 (0x40+1) // SEND_OP_COND (MMC)
#define ACMD41 (0xC0+41) // SEND_OP_COND (SDC)
#define CMD8 (0x40+8) // SEND_IF_COND
#define CMD9 (0x40+9) // SEND_CSD
#define CMD16 (0x40+16) // SET_BLOCKLEN
#define CMD17 (0x40+17) // READ_SINGLE_BLOCK
#define CMD24 (0x40+24) // WRITE_BLOCK
#define CMD55 (0x40+55) // APP_CMD
#define CMD58 (0x40+58) // READ_OCR
//--------------------------------------------------
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;
extern volatile uint16_t Timer1;
sd_info_ptr sdinfo;
char str1[60]={0};
//--------------------------------------------------
static void Error (void)
{
  LD_ON;
}
//----------------------------------------------- Ham ghi, doc du lieu tren bus SPI
uint8_t SPIx_WriteRead(uint8_t Byte)
{
  uint8_t receivedbyte = 0;
  if(HAL_SPI_TransmitReceive(&hspi2,(uint8_t*) &Byte,(uint8_t*) &receivedbyte,1,0x1000)!=HAL_OK)
  {
    Error();
  }
  return receivedbyte;
}
//----------------------------------------------- Ham truyen 1byte du lieu tren bus SPI
void SPI_SendByte(uint8_t bt)
{
  SPIx_WriteRead(bt);
}
//----------------------------------------------- Ham nhan 1byte du lieu tren bus SPI
uint8_t SPI_ReceiveByte(void)
{
  uint8_t bt = SPIx_WriteRead(0xFF);
  return bt;
}
//-----------------------------------------------
void SPI_Release(void)
{
  SPIx_WriteRead(0xFF);
}
//-----------------------------------------------
uint8_t SPI_wait_ready(void)
{
  uint8_t res;
  uint16_t cnt;
  cnt=0;
  do {
    res=SPI_ReceiveByte();
    cnt++;
  } while ( (res!=0xFF)&&(cnt<0xFFFF) );
  if (cnt>=0xFFFF) return 1;
  return res;
}
/*
	Cau lenh goi theo format of CMD8 - 48bit - 6bytes
*/
//----------------------------------------------- Goi 1 lenh den 
static uint8_t SD_cmd (uint8_t cmd, uint32_t arg)
{
  uint8_t n, res;
	//Phan biet lenh ACMD hay lenh CMD
	// ACMD<n> is the command sequense of CMD55-CMD<n>
	if (cmd & 0x80)
	{
		cmd &= 0x7F;
		res = SD_cmd(CMD55, 0);
		if (res > 1) return res;
	}
	// Select the card
	SS_SD_DESELECT();
	SPI_ReceiveByte();
	SS_SD_SELECT();
	SPI_ReceiveByte();
	// Send a command packet
	SPI_SendByte(cmd); // Start + Command index
	SPI_SendByte((uint8_t)(arg >> 24)); // Argument[31..24]
	SPI_SendByte((uint8_t)(arg >> 16)); // Argument[23..16]
	SPI_SendByte((uint8_t)(arg >> 8)); // Argument[15..8]
	SPI_SendByte((uint8_t)arg); // Argument[7..0]
	n = 0x01; // Dummy CRC + Stop
	//Cyclic Redundancy Check: Ham bam phat sinh kiem thu cho chuoi bit
	if (cmd == CMD0) {n = 0x95;} // Valid CRC for CMD0(0)
	if (cmd == CMD8) {n = 0x87;} // Valid CRC for CMD8(0x1AA)
	SPI_SendByte(n);	
  // Receive a command response
  n = 10; // Wait for a valid response in timeout of 10 attempts
  do {
    res = SPI_ReceiveByte();
  } while ((res & 0x80) && --n);
  return res;
}
//-----------------------------------------------
void SD_PowerOn(void)
{
  Timer1 = 0;
  while(Timer1<2) //cho 20mili giay, de on dinh dien ap
    ;
}
//-----------------------------------------------
uint8_t SD_Read_Block (uint8_t *buff, uint32_t lba)
{
  uint8_t result;
  uint16_t cnt;
	result=SD_cmd (CMD17, lba); //CMD17 datasheet trang 50 -> 96
	if (result!=0x00) return 5; //Thoat, neu ket qua tra ve 0x00
	  SPI_Release();
  cnt=0;
  do{ //We are waiting for the beginning of the block
    result = SPI_ReceiveByte();
    cnt++;
  } while ( (result!=0xFE)&&(cnt<0xFFFF) );
  if (cnt>=0xFFFF) return 5;
  for (cnt=0;cnt<512;cnt++) buff[cnt]=SPI_ReceiveByte(); //get the bytes of the block from the bus to the buffer
  SPI_Release(); 
  SPI_Release();
  return 0;
}
//-----------------------------------------------
uint8_t SD_Write_Block (uint8_t *buff, uint32_t lba)
{
  uint8_t result;
  uint16_t cnt;
  result=SD_cmd(CMD24,lba); //CMD24 Datasheet 51 and 97-98
  if (result!=0x00) return 6; //Exit if the result is not 0x00
  SPI_Release();
  SPI_SendByte (0xFE); //Beginning of the buffer (Start block token)
  for (cnt=0;cnt<512;cnt++) SPI_SendByte(buff[cnt]); //data transfer
  SPI_Release();
  SPI_Release();
  result=SPI_ReceiveByte();
  if ((result&0x05)!=0x05) return 6; //Exit if the result is not 0x05 (Datamount pg 111)
  cnt=0;
  do { //We are waiting for the end of the BUSY state
    result=SPI_ReceiveByte();
    cnt++;
  } while ( (result!=0xFF)&&(cnt<0xFFFF) );
  if (cnt>=0xFFFF) return 6;
  return 0;
}
//-----------------------------------------------
uint8_t sd_ini(void)
{
	uint8_t i, cmd;					//Command
  int16_t tmr;						//Virtual timer
  uint32_t temp;					//Bien tam
  LD_OFF;									//Tat led PC13
  sdinfo.type = 0;				//Kieu the
	//OCR Register
	uint8_t ocr[4];
	temp = hspi2.Init.BaudRatePrescaler;
	//Chia toc do cua SPI thang 156.25 KBPS vi toc doc cua bus APH qua cao nen sd card khong the dong bo
	hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128; //156.25 kbps
	HAL_SPI_Init(&hspi2);
	SS_SD_DESELECT();
	for(i=0;i<10;i++) //Can phai doi it nhat 74 xung de dong bo, o day minh set 80
	SPI_Release();
	hspi2.Init.BaudRatePrescaler = temp;
	HAL_SPI_Init(&hspi2);
	SS_SD_SELECT();
  if (SD_cmd(CMD0, 0) == 1) // Enter Idle state
  {
		SPI_Release();
		if (SD_cmd(CMD8, 0x1AA) == 1) // SD version2 (Card returns response without illegal command) Lenh hop le
		{
			for (i = 0; i < 4; i++) ocr[i] = SPI_ReceiveByte();
			sprintf(str1,"OCR: 0x%02X 0x%02X 0x%02X 0x%02X\r\n",ocr[0],ocr[1],ocr[2],ocr[3]);
			HAL_UART_Transmit(&huart1,(uint8_t*)str1,strlen(str1),0x1000);
			// Get trailing return value of R7 resp 
			//Check nguong dien ap hoat dong sd card
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) // The card can work at vdd range of 2.7-3.6V
			{
				  for (tmr = 12000; tmr && SD_cmd(ACMD41, 1UL << 30); tmr--)
						; // Wait for leaving idle state (ACMD41 with HCS bit)
					if (tmr && SD_cmd(CMD58, 0) == 0) { // Check CCS bit in the OCR
					for (i = 0; i < 4; i++) ocr[i] = SPI_ReceiveByte();
					sprintf(str1,"OCR: 0x%02X 0x%02X 0x%02X 0x%02X\r\n",ocr[0],ocr[1],ocr[2],ocr[3]);
					HAL_UART_Transmit(&huart1,(uint8_t*)str1,strlen(str1),0x1000);
						//Kiem tra neu 8bit OCR dau tien la Low voltage range thi SDv2 (0x04) + CT_BLOCK (0x08)
					sdinfo.type = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2; // SDv2 (HC or SC)
				}
			}
		}
		else //SD version1 or MMC version3 (illegal command) Lenh khong hop le
		{
			if (SD_cmd(ACMD41, 0) <= 1)
			{
				sdinfo.type = CT_SD1; cmd = ACMD41; // SDv1
			}
			else
			{
				sdinfo.type = CT_MMC; cmd = CMD1; // MMCv3
			}
			for (tmr = 25000; tmr && SD_cmd(cmd, 0); tmr--) ; // Wait for leaving idle state
			if (!tmr || SD_cmd(CMD16, 512) != 0) // Set R/W block length to 512
			sdinfo.type = 0;
		}
  }
  else
  {
    return 1;
  }
  sprintf(str1,"Type SD: 0x%02X\r\n",sdinfo.type);
  HAL_UART_Transmit(&huart1,(uint8_t*)str1,strlen(str1),0x1000);	
  return 0;
}
//-----------------------------------------------
