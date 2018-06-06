/* libusb-win32, Generic Windows USB Library
 * Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "stub_driver.h"
#include "stub_dbg.h"
#include "stub_irp.h"

static NTSTATUS
on_start_complete(DEVICE_OBJECT *devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)devobj->DeviceExtension;

	UNREFERENCED_PARAMETER(context);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	if (devstub->next_stack_dev->Characteristics & FILE_REMOVABLE_MEDIA) {
		devobj->Characteristics |= FILE_REMOVABLE_MEDIA;
	}
#if 0 ////////////////TODO
#ifndef SKIP_CONFIGURE_NORMAL_DEVICES
	// select initial configuration if not a filter
	if (!dev->is_filter && !dev->is_started)
	{
		// optionally, the initial configuration value can be specified
		// in the inf file. See reg_get_properties()
		// HKR,,"InitialConfigValue",0x00010001,<your config value>

		// If initial_config_value is negative, the configuration will
		// only be set if the device is not already configured.
		if (dev->initial_config_value)
		{
			if (dev->initial_config_value == SET_CONFIG_ACTIVE_CONFIG)
			{
				USBDBG("applying active configuration for %s\n",
					dev->device_id);
			}
			else
			{
				USBDBG("applying InitialConfigValue %d for %s\n",
					dev->initial_config_value, dev->device_id);
			}

			if(!NT_SUCCESS(set_configuration(dev, dev->initial_config_value, LIBUSB_DEFAULT_TIMEOUT)))
			{
				// we should always be able to apply the active configuration,
				// even in the case of composite devices.
				if (dev->initial_config_value == SET_CONFIG_ACTIVE_CONFIG)
				{
					USBERR("failed applying active configuration for %s\n",
						dev->device_id);
				}
				else
				{
					USBERR("failed applying InitialConfigValue %d for %s\n",
						dev->initial_config_value, dev->device_id);
				}
			}
		}
	}
#endif
#endif
	devstub->is_started = TRUE;
	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static NTSTATUS
on_device_usage_notification_complete(PDEVICE_OBJECT devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)devobj->DeviceExtension;

	UNREFERENCED_PARAMETER(context);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	if (!(devstub->next_stack_dev->Flags & DO_POWER_PAGABLE)) {
		devobj->Flags &= ~DO_POWER_PAGABLE;
	}

	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static NTSTATUS
on_query_capabilities_complete(PDEVICE_OBJECT devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)devobj->DeviceExtension;
	IO_STACK_LOCATION	*irpstack = IoGetCurrentIrpStackLocation(irp);

	UNREFERENCED_PARAMETER(context);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	if (NT_SUCCESS(irp->IoStatus.Status)) {
#if 0 ////TODO
		if (!devstub->is_filter) {
			/* apply registry setting */
			irpstack->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK =
				devstub->surprise_removal_ok;
		}
#endif

		/* save supported device power states */
		memcpy(devstub->device_power_states, irpstack->Parameters.DeviceCapabilities.Capabilities->DeviceState,
			sizeof(devstub->device_power_states));
	}

	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static void
disable_interface(usbip_stub_dev_t *devstub)
{
	NTSTATUS	status;

	///TODO set_filter_interface_key(dev, (ULONG)-1);

	status = IoSetDeviceInterfaceState(&devstub->interface_name, FALSE);
	if (NT_ERROR(status)) {
		DBGE(DBG_PNP, "failed to disable interface: err: %s\n", dbg_ntstatus(status));
	}
	if (devstub->interface_name.Buffer) {
		RtlFreeUnicodeString(&devstub->interface_name);
		devstub->interface_name.Buffer = NULL;
	}
}

NTSTATUS
stub_dispatch_pnp(usbip_stub_dev_t *devstub, IRP *irp)
{
	IO_STACK_LOCATION	*irpstack = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS	status;

	DBGI(DBG_DISPATCH, "dispatch_pnp: minor: %s\n", dbg_pnp_minor(irpstack->MinorFunction));

	status = lock_dev_removal(devstub);
	if (NT_ERROR(status)) {
		DBGI(DBG_PNP, "device is pending removal: %s\n", dbg_devstub(devstub));
		return complete_irp(irp, status, 0);
	}

	switch (irpstack->MinorFunction) {
	case IRP_MN_REMOVE_DEVICE:
		if (devstub->is_started) {
			disable_interface(devstub);
		}

		devstub->is_started = FALSE;

		/* wait until all outstanding requests are finished */
		unlock_wait_dev_removal(devstub);

		status = pass_irp_down(devstub, irp, NULL, NULL);

		DBGI(DBG_PNP, "deleting device: %s\n", dbg_devstub(devstub));

		remove_devlink(devstub);

#if 0 ////TODO
		UpdateContextConfigDescriptor(dev,NULL,0,0,-1);
#endif
		/* delete the device object */
		IoDetachDevice(devstub->next_stack_dev);
		IoDeleteDevice(devstub->self);

		return status;
	case IRP_MN_SURPRISE_REMOVAL:
		devstub->is_started = FALSE;

		disable_interface(devstub);
#if 0 /////TODO
		UpdateContextConfigDescriptor(dev,NULL,0,0,-1);
#endif
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_START_DEVICE:
		// A driver calls this routine after receiving a device set-power 
		// request and before calling PoStartNextPowerIrp. When handling a 
		// PnP IRP_MN_START_DEVICE request, the driver should call 
		// PoSetPowerState to notify the power manager that the device is 
		// in the D0 state.
		//
		PoSetPowerState(devstub->self, DevicePowerState, devstub->power_state);

		status = IoSetDeviceInterfaceState(&devstub->interface_name, TRUE);
		if (NT_ERROR(status)) {
			DBGE(DBG_PNP, "failed to enable interface: err: %s\n", dbg_ntstatus(status));
		}
		return pass_irp_down(devstub, irp, on_start_complete, NULL);
	case IRP_MN_STOP_DEVICE:
		devstub->is_started = FALSE;
		break;
	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		if (!devstub->self->AttachedDevice || (devstub->self->AttachedDevice->Flags & DO_POWER_PAGABLE)) {
			devstub->self->Flags |= DO_POWER_PAGABLE;
		}

		return pass_irp_down(devstub, irp, on_device_usage_notification_complete, NULL);
	case IRP_MN_QUERY_CAPABILITIES:
#if 0 ///TODO
		if (!devstub->is_filter) {
			/* apply registry setting */
			irpstack->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = devstub->surprise_removal_ok;
		}
#endif
		return pass_irp_down(devstub, irp, on_query_capabilities_complete,  NULL);
	default:
		break;
	}

	unlock_dev_removal(devstub);
	return pass_irp_down(devstub, irp, NULL, NULL);
}