#include "main.h"
#include "rtc.h"
#ifdef SYSTEM_STM32
#include "stm32f10x.h"
#include "i2c.h"
#include "printf.h"
#include "power.h"
#endif

uint8_t isleapyear(uint16_t);
#ifdef SYSTEM_STM32
void Time_Adjust(tm_t*);
void RTC_Configuration(void);
void NVIC_Configuration(void);
uint32_t FtimeToCounter(tm_t *);
void NVIC_GenerateSystemReset(void);
#endif
extern count_t count;
extern state_t state;
extern config_t config;
//extern uint8_t stateMain;
extern uint32_t i2cTimeLimit;
uint8_t isleapyear(uint16_t);

tm_t * dateTimep_p; //Структура с живой датой и временем
//time_t time_p = &dateTime;
tm_t dateTime;

uint8_t lastdaysofmonths[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*******************************************************************************
 *Сколько дней в месяце
 ******************************************************************************/
uint8_t lastdayofmonth(uint16_t year, uint8_t month) {
	if (month == 2 && isleapyear(year))
		return 29;
	return lastdaysofmonths[month];
}

/*******************************************************************************
 *Какой день недели
 ******************************************************************************/
uint8_t weekDay(uint8_t day, uint8_t month, uint16_t year) {
	uint8_t a;
	uint8_t m;
	uint16_t y;
	day += 5; //Первым будит понедельник
	a = (14 - month) / 12;
	y = year - a;
	m = month + 12 * a - 2;
	return ((7000 + (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12)) + 1) % 7;
}

/*******************************************************************************
 *Проверка на високосность
 ******************************************************************************/
uint8_t isleapyear(uint16_t year) {
	if (year % 400 == 0)
		return 1;
	if (year % 100 == 0)
		return 0;
	return year % 4 == 0;
}

/*******************************************************************************
 *Установка или Взятие времени
 ******************************************************************************/
tm_t * timeGetSet(tm_t * t) {
	if (!t) {
#ifdef SYSTEM_WIN
		time_t t = time(NULL);
		return localtime(&t);
	} else
		return NULL;
#endif
#ifdef SYSTEM_STM32
	return dateTimep_p;
} else {
	Time_Adjust(t);
	state.taskList |= TASK_UPDATETIME;
	return NULL;
}
#endif
}

/*******************************************************************************
 *Запуск часов
 ******************************************************************************/
void RTC_init(void) {
#ifdef SYSTEM_STM32
	/* NVIC configuration */
	NVIC_Configuration(); //Настрока прерываний для часов
	if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5) {
		/* Backup data register value is not correct or not yet programmed (when
		 the first time the program is executed) */
		printf("%s", "\r\n\n RTC not yet configured....");

//		uint8_t	tm_sec;		/* Seconds: 0-59 (K&R says 0-61?) */
//		uint8_t	tm_min;		/* Minutes: 0-59 */
//		uint8_t	tm_hour;	/* Hours since midnight: 0-23 */
//		uint8_t	tm_mday;	/* Day of the month: 1-31 */
//		uint8_t	tm_mon;		/* Months *since* january: 0-11 */
//		uint16_t tm_year;	/* Years since 1900 */
//		uint8_t	tm_wday;	/* Days since Sunday (0-6) */
//		uint16_t tm_yday;	/* Days since Jan. 1: 0-365 */
//		uint8_t	tm_isdst;	/* +1 Daylight Savings Time, 0 No DST,*/

		dateTime.tm_year = 115;
		dateTime.tm_mon = 1;
		dateTime.tm_mday = 1;
		dateTime.tm_hour = 12;
		dateTime.tm_min = 0;
		dateTime.tm_sec = 0;

		/* Adjust time by values entred by the user on the hyperterminal */
		Time_Adjust(&dateTime);

		state.taskList |= TASK_TIMESETUP;
	} else {
		/* Check if the Power On Reset flag is set */
		if (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET) {
			printf("%s", "\r\n\n Power On Reset occurred....");
		}
		/* Check if the Pin Reset flag is set */
		else if (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET) {
			printf("%s", "\r\n\n External Reset occurred....");
		}
		printf("%s", "\r\n No need to configure RTC....");

		/* Wait for RTC registers synchronization */
		RTC_WaitForSynchro();

		/* Enable the RTC Second */
		RTC_ITConfig(RTC_IT_SEC, ENABLE);
		/* Wait until last write operation on RTC registers has finished */
		RTC_WaitForLastTask();

		/* Clear reset flags */
		RCC_ClearFlag();
	}

	//Настроим указатель на структуру и обновим время в структуре
	dateTimep_p = &dateTime;
	CounterToFtime(RTC_GetCounter(), &dateTime);
#endif
}

#ifdef SYSTEM_STM32
/**
 * @brief  Configures the nested vectored interrupt controller.
 * @param  None
 * @retval None
 */
void NVIC_Configuration(void) {
	NVIC_InitTypeDef NVIC_InitStructure;

	/* Configure one bit for preemption priority */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

	/* Enable the RTC Interrupt */
	NVIC_InitStructure.NVIC_IRQChannel = RTC_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

void Time_Adjust(tm_t* time) {
	/* RTC Configuration */
	RTC_Configuration();
	printf("%s", "\r\n RTC configured....");

	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();
	/* Change the current time */
	RTC_SetCounter(FtimeToCounter(time));
	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();

	BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);

	/* Clear reset flags */
	RCC_ClearFlag();

	state.taskList |= TASK_UPDATETIME;
}

/*******************************************************************************

 ******************************************************************************/
void RTC_Configuration(void) {
	/* Enable PWR and BKP clocks */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);

	/* Allow access to BKP Domain */
	PWR_BackupAccessCmd(ENABLE);

	/* Reset Backup Domain */
	BKP_DeInit();

	/* Enable LSE */
	RCC_LSEConfig(RCC_LSE_ON);
	/* Wait till LSE is ready */
	while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET) {
	}

	/* Select LSE as RTC Clock Source */
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);

	/* Enable RTC Clock */
	RCC_RTCCLKCmd(ENABLE);

	/* Wait for RTC registers synchronization */
	RTC_WaitForSynchro();

	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();

	/* Enable the RTC Second */
	RTC_ITConfig(RTC_IT_SEC, ENABLE);

	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();

	/* Set RTC prescaler: set RTC period to 1sec */
	RTC_SetPrescaler(32767); /* RTC period = RTCCLK/RTC_PR = (32.768 KHz)/(32767+1) */

	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();
}

//They probably just changed the name. It was in the library source file stm32f10x_nvic.c

#define AIRCR_VECTKEY_MASK    ((u32)0x05FA0000)

/*******************************************************************************
 * Function Name  : NVIC_GenerateSystemReset
 * Description    : Generates a system reset.
 * Input          : None
 * Output         : None
 * Return         : None
 ******************************************************************************/
void NVIC_GenerateSystemReset(void) {
	SCB->AIRCR = AIRCR_VECTKEY_MASK | (u32) 0x04;
}

/*******************************************************************************
 Ежесекундное прерывание от часов
 ******************************************************************************/
void RTC_IRQHandler(void) {
	if (RTC_GetITStatus(RTC_IT_SEC) != RESET) {
		/* Clear the RTC Second interrupt */
		RTC_ClearITPendingBit(RTC_IT_SEC);

		if(!(state.taskList & TASK_DRIVE) && config.SleepSec) { //Если не едим
			if((count.toSleep > config.SleepSec) && (state.powerMode == POWERMODE_NORMAL)) { //Не пора ли спать?
				setPowerMode(POWERMODE_SLEEP);//Спать готов
			}
			else count.toSleep++; //Приблизиться ко сну
		}

		/*Обновим время, если не спящий режим*/
		if(state.powerMode != POWERMODE_SLEEP) {
			if (state.taskList & TASK_UPDATETIME) {
				CounterToFtime(RTC_GetCounter(), &dateTime);
				state.taskList &=~ TASK_UPDATETIME;
			}
			else {
				dateTime.tm_sec++;
				if (dateTime.tm_sec == 60) {
					dateTime.tm_sec = 0;
					dateTime.tm_min++;
					if (dateTime.tm_min == 60) {
						dateTime.tm_min = 0;
						dateTime.tm_hour++;
						if (dateTime.tm_hour == 24) {
							CounterToFtime(RTC_GetCounter(), &dateTime);
						}
					}
					state.taskList |= TASK_REDRAW; //Перерисовка каждую минуту
				}
				if(config.SecInTime)state.taskList |= TASK_REDRAW; //Перерисовка если показываем секунды
			}
		}

		if (i2cTimeLimit > 1000000) //Если i2c висит
		NVIC_GenerateSystemReset();//Ребут
	}
}
#endif

/*******************************************************************************
 *Преобразование значение счетчика в григорианскую дату и время
 ******************************************************************************/
void CounterToFtime(uint32_t counter, tm_t * dateTime) {
	uint32_t ace;
	uint8_t b;
	uint8_t d;
	uint8_t m;

	ace = (counter / 86400) + 32044 + JD0;
	b = (4 * ace + 3) / 146097; // может ли произойти потеря точности из-за переполнения 4*ace ??
	ace = ace - ((146097 * b) / 4);
	d = (4 * ace + 3) / 1461;
	ace = ace - ((1461 * d) / 4);
	m = (5 * ace + 2) / 153;
	dateTime->tm_mday = ace - ((153 * m + 2) / 5) + 1;
	dateTime->tm_mon = m + 2 - (12 * (m / 10));
	dateTime->tm_year = (100 * b + d - 4800 + (m / 10)) - 1900;
	dateTime->tm_hour = (counter / 3600) % 24;
	dateTime->tm_min = (counter / 60) % 60;
	dateTime->tm_sec = (counter % 60);
//	dateTime->tm_wday = weekDay(dateTime->tm_mday, dateTime->tm_mon, dateTime->tm_year + 1900);
}


/*******************************************************************************
 *Преобразование григорианской даты и времени в значение счетчика
 ******************************************************************************/
uint32_t FtimeToCounter(tm_t * dateTime) {
	uint8_t a;
	uint16_t y;
	uint8_t m;
	uint32_t JDN;
// Вычисление необходимых коэффициентов
	a = (15 - dateTime->tm_mon) / 12;
	y = dateTime->tm_year + 1900 + 4800 - a;
	m = dateTime->tm_mon + (12 * a) - 2;
// Вычисляем значение текущего Юлианского дня
	JDN = dateTime->tm_mday;
	JDN += (153 * m + 2) / 5;
	JDN += 365 * y;
	JDN += y / 4;
	JDN += -y / 100;
	JDN += y / 400;
	JDN -= 32045;
	JDN -= JD0;// так как счетчик у нас нерезиновый, уберем дни которые прошли до 01 янв 2001
	JDN *= 86400;// переводим дни в секунды
	JDN += (dateTime->tm_hour * 3600);// и дополняем его секундами текущего дня
	JDN += (dateTime->tm_min * 60);
	JDN += (dateTime->tm_sec);
// итого имеем количество секунд с 00-00 01 янв 2001
	return JDN;
}
