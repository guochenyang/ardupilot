/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#include <AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_PX4
#include "AP_InertialSensor_PX4.h"

const extern AP_HAL::HAL& hal;

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <drivers/drv_accel.h>
#include <drivers/drv_gyro.h>

Vector3f AP_InertialSensor_PX4::_accel_sum;
uint32_t AP_InertialSensor_PX4::_accel_sum_count;
Vector3f AP_InertialSensor_PX4::_gyro_sum;
uint32_t AP_InertialSensor_PX4::_gyro_sum_count;
volatile bool AP_InertialSensor_PX4::_in_accumulate;
uint64_t AP_InertialSensor_PX4::_last_timestamp;
int AP_InertialSensor_PX4::_accel_fd;
int AP_InertialSensor_PX4::_gyro_fd;

uint16_t AP_InertialSensor_PX4::_init_sensor( Sample_rate sample_rate ) 
{
    switch (sample_rate) {
    case RATE_50HZ:
        _sample_divider = 4;
        break;
    case RATE_100HZ:
        _sample_divider = 2;
        break;
    case RATE_200HZ:
    default:
        _sample_divider = 1;
        break;
    }

	// init accelerometers
	_accel_fd = open(ACCEL_DEVICE_PATH, O_RDONLY);
	if (_accel_fd < 0) {
        hal.scheduler->panic("Unable to open accel device " ACCEL_DEVICE_PATH);
    }

	_gyro_fd = open(GYRO_DEVICE_PATH, O_RDONLY);
	if (_gyro_fd < 0) {
        hal.scheduler->panic("Unable to open gyro device " GYRO_DEVICE_PATH);
    }

    /* 
     * set the accel and gyro sampling rate. We always set these to
     * 200 then average in this driver
     */
    ioctl(_accel_fd, ACCELIOCSSAMPLERATE, 200);
    ioctl(_accel_fd, SENSORIOCSPOLLRATE,  200);
    ioctl(_gyro_fd,  GYROIOCSSAMPLERATE,  200);
    ioctl(_gyro_fd,  SENSORIOCSPOLLRATE,  200);

    // ask for a 10 sample buffer. The mpu6000 PX4 driver doesn't
    // support this yet, but when it does we want to use it
    ioctl(_accel_fd, SENSORIOCSQUEUEDEPTH, 10);
    ioctl(_gyro_fd,  SENSORIOCSQUEUEDEPTH, 10);

    // register a 1kHz timer to read from PX4 sensor drivers
    hal.scheduler->register_timer_process(_ins_timer);

    return AP_PRODUCT_ID_PX4;
}

/*================ AP_INERTIALSENSOR PUBLIC INTERFACE ==================== */

bool AP_InertialSensor_PX4::update(void) 
{
    while (num_samples_available() == 0) {
        hal.scheduler->delay(1);
    }
    Vector3f accel_scale = _accel_scale.get();

    hal.scheduler->suspend_timer_procs();

    // base the time on the number of gyro samples, as that is what is
    // multiplied by time to integrate in DCM
    _delta_time = _gyro_sum_count / 200.0;
    _last_update_usec = (uint32_t)_last_timestamp;

    _accel = _accel_sum / _accel_sum_count;
    _accel_sum.zero();
    _accel_sum_count = 0;

    _gyro = _gyro_sum / _gyro_sum_count;
    _gyro_sum.zero();
    _gyro_sum_count = 0;

    hal.scheduler->resume_timer_procs();

    // add offsets and rotation
    _accel.rotate(_board_orientation);
    _accel.x *= accel_scale.x;
    _accel.y *= accel_scale.y;
    _accel.z *= accel_scale.z;
    _accel   -= _accel_offset;

    _gyro.rotate(_board_orientation);
    _gyro -= _gyro_offset;

    return true;
}

bool AP_InertialSensor_PX4::new_data_available(void) 
{
    return num_samples_available() > 0;
}


float AP_InertialSensor_PX4::temperature(void) 
{
    return 0.0;
}

float AP_InertialSensor_PX4::get_delta_time(void) 
{
    return _delta_time;
}

uint32_t AP_InertialSensor_PX4::get_last_sample_time_micros(void) 
{
    return _last_update_usec;
}

float AP_InertialSensor_PX4::get_gyro_drift_rate(void) 
{
    // 0.5 degrees/second/minute
    return ToRad(0.5/60);
}

void AP_InertialSensor_PX4::_accumulate(void)
{
    struct accel_report	accel_report;
    struct gyro_report	gyro_report;

    if (_in_accumulate) {
        return;
    }
    _in_accumulate = true;

    if (::read(_accel_fd, &accel_report, sizeof(accel_report)) == sizeof(accel_report)) {
        _accel_sum += Vector3f(accel_report.x, accel_report.y, accel_report.z);
        _accel_sum_count++;
	}

    if (::read(_gyro_fd, &gyro_report, sizeof(gyro_report)) == sizeof(gyro_report)) {
        _gyro_sum += Vector3f(gyro_report.x, gyro_report.y, gyro_report.z);
        _gyro_sum_count++;
        _last_timestamp = gyro_report.timestamp;
	}
    _in_accumulate = false;
}

void AP_InertialSensor_PX4::_ins_timer(uint32_t now)
{
    _accumulate();
}

uint16_t AP_InertialSensor_PX4::num_samples_available(void)
{
    _accumulate();
    return min(_accel_sum_count, _gyro_sum_count) / _sample_divider;
}

#endif // CONFIG_HAL_BOARD

