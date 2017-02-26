// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "date.h"
#include <time.h>

/*
 * Get the current year
 */
int date_get_year(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_year + 1900;
}

/*
 * Get the current month
 */
int date_get_month(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_mon + 1;
}

/*
 * Get the day of the year
 */
int date_get_dayofyear(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_yday;
}

/*
 * Get the current hours
 */
int date_get_hour(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_hour;
}

/*
 * Get the current minutes
 */
int date_get_min(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_min;
}

/*
 * Get the current seconds
 */
int date_get_sec(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	return lt->tm_sec;
}

/*
 * Is today a day of the Sun for Star Gladiators?
 */
int is_day_of_sun(void)
{
	return date_get_dayofyear()%2 == 0;
}

/*
 * Is today a day of the Moon for Star Gladiators?
 */
int is_day_of_moon(void)
{
	return date_get_dayofyear()%2 == 1;
}

/*
 * Is today a day of the Star for Star Gladiators?
 */
int is_day_of_star(void)
{
	return date_get_dayofyear()%5 == 0;
}
