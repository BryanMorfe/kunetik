#ifndef __U_KUNETIK_H__
#define __U_KUNETIK_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/** Constants **/
#define KTK_TEMPTYPE_OFFSET (0x00)
#define KTK_TEMP_OFFSET     (0x01)
#define KTK_HMDT_OFFSET     (0x02)
#define KTK_DATA_SIZE       (0x04)

/** Types **/

enum
{
    KTK_TEMP_TYPE_CELCIUS,
    KTK_TEMP_TYPE_FAHRENHEIT,

    KTK_TEMP_TYPE_MAX
};

struct kunetik_temp_type
{
    __u8 type;
};

/** IOCTL **/

#define KTK_SET_TEMP_TYPE _IOW('V', 0, struct kunetik_temp_type)
#define KTK_GET_TEMP_TYPE _IOR('V', 1, struct kunetik_temp_type)
#define KTK_CAPTURE_DATA  _IO('V', 2)

#endif /* __U_KUNETIK_H__ */