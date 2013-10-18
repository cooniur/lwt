//
//  lwt_trampoline.s
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

.text
.align 16
.globl __lwt_trampoline

__lwt_trampoline:
	
	jmp __lwt_start

