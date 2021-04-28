// Uses GCLK6, TCC1, pin PA19 = D12, interrupt 3, DMA channels 0 and 1, Event System channel 0/1
// Interferes with TCC0 clock setup

uint32_t clockFrequency;
volatile uint32_t period = 4294967295;
volatile uint32_t pulseWidth = 0;
typedef struct {
  uint16_t btctrl;
  uint16_t btcnt;
  uint32_t srcaddr;
  uint32_t dstaddr;
  uint32_t descaddr;
} dmacdescriptor;
volatile dmacdescriptor wrb[12] __attribute__ ((aligned (16)));        // Write-back DMAC descriptors
dmacdescriptor descriptor_section[12] __attribute__ ((aligned (16)));  // DMAC channel descriptors
dmacdescriptor descriptor __attribute__ ((aligned (16)));              // Place holder descriptor

void setup() {
  // Set up the USB port
  SerialUSB.begin(1000000);
  while(!SerialUSB);

  // Set up the clock for TCC1
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(1) |                               // Divide the 48MHz system clock by 1 for 48MHz
                    GCLK_GENDIV_ID(6);                                 // Set division on GCLK6
  while (GCLK->STATUS.bit.SYNCBUSY);                                   // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |                                // Set the duty cycle to 50%
                     GCLK_GENCTRL_GENEN |                              // Enable GCLK
                     GCLK_GENCTRL_SRC_DFLL48M |                        // Set the clock source to 48MHz
                     GCLK_GENCTRL_ID(6);                               // Set clock source GCLK6
  while (GCLK->STATUS.bit.SYNCBUSY);                                   // Wait for synchronization

  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |                              // Enable the generic clock
                     GCLK_CLKCTRL_GEN_GCLK6 |                          // on GCLK6
                     GCLK_CLKCTRL_ID_TCC0_TCC1;                        // Feed the GCLK to TCC0 and TCC1
  while (GCLK->STATUS.bit.SYNCBUSY);                                   // Wait for synchronization

  // Enable the port multiplexer on pin D12
  PORT->Group[g_APinDescription[12].ulPort].PINCFG[g_APinDescription[12].ulPin].bit.PMUXEN = 1;

  // Set up pin D12 as an EIC peripheral on pin D12 (uses EXTINT[3])
  PORT->Group[g_APinDescription[12].ulPort].PMUX[g_APinDescription[12].ulPin >> 1].reg |= PORT_PMUX_PMUXO_A;

  // Set up the Event System
  REG_PM_APBCMASK |= PM_APBCMASK_EVSYS;                                // Switch on the event system peripheral
  REG_EIC_EVCTRL |= EIC_EVCTRL_EXTINTEO3;                              // Enable event output on external interrupt 3
  REG_EIC_CONFIG0 |= EIC_CONFIG_SENSE3_HIGH;                           // Set event detecting a HIGH level
  REG_EIC_CTRL |= EIC_CTRL_ENABLE;                                     // Enable EIC peripheral
  while (EIC->STATUS.bit.SYNCBUSY);                                    // Wait for synchronization

  REG_EVSYS_USER = EVSYS_USER_CHANNEL(1) |                             // Attach the event user (receiver) to channel 0 (n + 1)
                   EVSYS_USER_USER(EVSYS_ID_USER_TCC1_EV_1);           // Set the event user (receiver) as timer TCC1, event 1

  REG_EVSYS_CHANNEL = EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT |             // No event edge detection
                      EVSYS_CHANNEL_PATH_ASYNCHRONOUS |                // Set event path as asynchronous
                      EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_EIC_EXTINT_3) | // Set event generator (sender) as external interrupt 3
                      EVSYS_CHANNEL_CHANNEL(0);                        // Attach the generator (sender) to channel 0

  // Set up TCC1
  REG_TCC1_EVCTRL |= TCC_EVCTRL_MCEI1 |                                // Enable the match or capture channel 1 event input
                     TCC_EVCTRL_MCEI0 |                                // Enable the match or capture channel 0 event input
                     TCC_EVCTRL_TCEI1 |                                // Enable the TCC event 1 input
                     TCC_EVCTRL_EVACT1_PPW;                            // Set up the timer for capture: CC0 period, CC1 pulsewidth

  REG_TCC1_CTRLA |= TCC_CTRLA_CPTEN1 |                                 // Enable capture on CC1
                    TCC_CTRLA_CPTEN0 |                                 // Enable capture on CC0
                    TCC_CTRLA_PRESCALER_DIV1 |                         // Set prescaler to 1 for 48MHz
                    TCC_CTRLA_ENABLE;                                  // Enable TCC
  while (TCC1->SYNCBUSY.bit.ENABLE);                                   // Wait for synchronization
  
  clockFrequency = 32768 * 1464;                                       // 48Mhz; replace with measured clock frequency

  // Set up the DMA Controller
  DMAC->BASEADDR.reg = (uint32_t)descriptor_section;                   // Set the descriptor section base address
  DMAC->WRBADDR.reg = (uint32_t)wrb;                                   // Set the write-back descriptor base adddess
  DMAC->CTRL.reg = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN(0xf);         // Enable the DMAC and priority levels

  DMAC->CHID.reg = DMAC_CHID_ID(0);                                    // Select DMAC channel 0
  DMAC->CHCTRLB.reg = DMAC_CHCTRLB_LVL(0) |                            // Set DMAC channel 0 to priority level 0 (lowest),
                      DMAC_CHCTRLB_TRIGSRC(TCC1_DMAC_ID_MC_0) |        // to trigger on TCC1 Match Compare 0 and
                      DMAC_CHCTRLB_TRIGACT_BEAT;                       // to trigger every beat
  descriptor.descaddr = (uint32_t)&descriptor_section[0];              // Set up a circular descriptor
  descriptor.srcaddr = (uint32_t)&TCC1->CC[0].reg;                     // Take the contents of the TCC1 Counter Compare 0 register
  descriptor.dstaddr = (uint32_t)&period;                              // Copy it to the "period" variable
  descriptor.btcnt = 1;                                                // This takes 1 beat
  descriptor.btctrl = DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_VALID;   // Copy 32-bits (WORD) and flag the descriptor as valid
  memcpy(&descriptor_section[0], &descriptor, sizeof(dmacdescriptor)); // Copy to the channel 0 descriptor
  DMAC->CHCTRLA.reg |= DMAC_CHCTRLA_ENABLE;                            // Enable DMAC channel 0

  DMAC->CHID.reg = DMAC_CHID_ID(1);                                    // Select DMAC channel 1
  DMAC->CHCTRLB.reg = DMAC_CHCTRLB_LVL(0) |                            // Set DMAC channel 1 to priority level 0 (lowest),
                      DMAC_CHCTRLB_TRIGSRC(TCC1_DMAC_ID_MC_1) |        // to trigger on TCC1 Match Compare 1 and
                      DMAC_CHCTRLB_TRIGACT_BEAT;                       // to trigger every beat
  descriptor.descaddr = (uint32_t)&descriptor_section[1];              // Set up a circular descriptor
  descriptor.srcaddr = (uint32_t)&TCC1->CC[1].reg;                     // Take the contents of the TCC1 Counter Compare 1 register
  descriptor.dstaddr = (uint32_t)&pulseWidth;                          // Copy it to the "pulseWidth" variable
  descriptor.btcnt = 1;                                                // This takes 1 beat
  descriptor.btctrl = DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_VALID;   // Copy 32-bits (WORD) and flag the descriptor as valid
  memcpy(&descriptor_section[1], &descriptor, sizeof(dmacdescriptor)); // Copy to the channel 1 descriptor
  DMAC->CHCTRLA.reg |= DMAC_CHCTRLA_ENABLE;                            // Enable DMAC channel 1

  delay(1);
}

void loop() {
  SerialUSB.print(round((1.0 * clockFrequency) / period)); SerialUSB.print("Hz, ");
  SerialUSB.print(round((1000000.0 * pulseWidth) / clockFrequency)); SerialUSB.print("μs pulse width, ");
  SerialUSB.print(round(100.0 * pulseWidth / period)); SerialUSB.println("% duty cycle");
  period = 4294967295;
  pulseWidth = 0;
  delay(500);
}
