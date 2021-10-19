## General
- Added dedicated script to uninstall libcerver

## Connection
- Setting connection disconnected state after ended

## Handler
- Added balancer handler to receive packet data

## Balancer
- Refactored custom balancer methods & types
- Added ability to call a cb when service status changes
- Resetting connection every time before reconnecting
- Added dedicated ended service status definition
- Added ability to handle packets on unavailable services

## Examples
- Added method to send test packet in example app
- Updated example app related definitions & methods
- Updated balancer example with latest callback methods

## Tests
- Added more balancer & services methods unit tests