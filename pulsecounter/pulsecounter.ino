// Setup TC4 in 32-bit mode to count incoming pulses on digital pin D12 using the Event System
void setup() {
  SerialUSB.begin(115200);
  while(!SerialUSB);

  // Generic Clock 
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |                 // Enable the generic clock
                      GCLK_CLKCTRL_GEN_GCLK0 |             // on GCLK0 at 48MHz
                      GCLK_CLKCTRL_ID_TC4_TC5;             // Route GCLK0 to TC4 and TC5
  while (GCLK->STATUS.bit.SYNCBUSY);                       // Wait for synchronization

  // Enable the port multiplexer on digital pin D12 (port pin PA19)
  PORT->Group[g_APinDescription[12].ulPort].PINCFG[g_APinDescription[12].ulPin].bit.PMUXEN = 1;
  // Set-up the pin as an EIC (interrupt) peripheral on D12
  PORT->Group[g_APinDescription[12].ulPort].PMUX[g_APinDescription[12].ulPin >> 1].reg |= PORT_PMUX_PMUXO_A;

  // External Interrupt Controller (EIC)
  EIC->EVCTRL.reg |= EIC_EVCTRL_EXTINTEO3;                 // Enable event output on external interrupt 3 (D12)
  EIC->CONFIG[0].reg |= EIC_CONFIG_SENSE3_HIGH;            // Set event detecting a HIGH level on interrupt 3
  EIC->INTENCLR.reg = EIC_INTENCLR_EXTINT3;                // Disable interrupts on interrupt 3
  EIC->CTRL.bit.ENABLE = 1;                                // Enable the EIC peripheral
  while (EIC->STATUS.bit.SYNCBUSY);                        // Wait for synchronization

  // Event System
  PM->APBCMASK.reg |= PM_APBCMASK_EVSYS;                                  // Switch on the event system peripheral
  EVSYS->USER.reg = EVSYS_USER_CHANNEL(1) |                               // Attach the event user (receiver) to channel 0 (n + 1)
                    EVSYS_USER_USER(EVSYS_ID_USER_TC4_EVU);               // Set the event user (receiver) as timer TC4
  EVSYS->CHANNEL.reg = EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT |               // No event edge detection
                       EVSYS_CHANNEL_PATH_ASYNCHRONOUS |                  // Set event path as asynchronous
                       EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_EIC_EXTINT_3) |   // Set event generator (sender) as external interrupt 3
                       EVSYS_CHANNEL_CHANNEL(0);                          // Attach the generator (sender) to channel 0                                 
 
  // Timer Counter TC4
  TC4->COUNT32.EVCTRL.reg |= TC_EVCTRL_TCEI |              // Enable asynchronous events on the TC timer
                             TC_EVCTRL_EVACT_COUNT;        // Increment the TC timer each time an event is received
  TC4->COUNT32.CTRLA.reg = TC_CTRLA_MODE_COUNT32;          // Configure TC4 together with TC5 to operate in 32-bit mode
  TC4->COUNT32.CTRLA.bit.ENABLE = 1;                       // Enable TC4
  while (TC4->COUNT32.STATUS.bit.SYNCBUSY);                // Wait for synchronization
  TC4->COUNT32.READREQ.reg = TC_READREQ_RCONT |            // Enable a continuous read request
                             TC_READREQ_ADDR(0x10);        // Offset of the 32-bit COUNT register
  while (TC4->COUNT32.STATUS.bit.SYNCBUSY);                // Wait for synchronization
}

void loop() {
  Serial.println(TC4->COUNT32.COUNT.reg);                  // Output the results
  TC4->COUNT32.CTRLBSET.reg = TC_CTRLBSET_CMD_RETRIGGER;   // Retrigger the TC4 timer
  while (TC4->COUNT32.STATUS.bit.SYNCBUSY);                // Wait for synchronization
  delay(1000);                                             // Wait for 1 second
}
