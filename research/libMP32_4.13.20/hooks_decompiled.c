// Ghidra 12.2 decompile of research/libMP32.so (MP 4.13.20): the hook functions (HookOf_*) and MP::MenuMp* / loading helpers.

// ===== HookOf_CGame_Process @ 0061316c

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* HookOf_CGame_Process() */

void HookOf_CGame_Process(void)

{
  FUN_00613190();
  (*(code *)**(undefined4 **)(DAT_0061318c + 0x61317a))();
  ScriptGuard::Tick();
                    /* WARNING: Could not recover jumptable at 0x009d4548. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)PTR_Process_009f7710)();
  return;
}


// ===== HookOf_CCamera_Process @ 00613314

/* HookOf_CCamera_Process(void*) */

void HookOf_CCamera_Process(void *param_1)

{
  Game::IdleCamGuard(true);
  (*(code *)**(undefined4 **)(DAT_00613338 + 0x613326))(param_1);
  (*(code *)PTR_IdleCamGuard_009f771c)(0);
  return;
}


// ===== HookOf_CCam_Process @ 0061333c

/* HookOf_CCam_Process(void*) */

void HookOf_CCam_Process(void *param_1)

{
  int iVar1;
  int iVar2;
  code *UNRECOVERED_JUMPTABLE_00;
  
  if ((*(int **)(DAT_006133a0 + 0x613346) != (int *)0x0) &&
     (*(undefined4 **)(DAT_006133a4 + 0x61334e) != (undefined4 *)0x0)) {
    iVar1 = **(int **)(DAT_006133a0 + 0x613346);
    UNRECOVERED_JUMPTABLE_00 = (code *)**(undefined4 **)(DAT_006133a4 + 0x61334e);
    iVar2 = 0;
    if (iVar1 != 0) {
      iVar2 = *(int *)(iVar1 + 700);
    }
    if ((iVar1 == 0 || iVar2 == 0) && (UNRECOVERED_JUMPTABLE_00 != (code *)0x0)) {
      iVar2 = *(int *)(DAT_006133a8 + 0x613368);
      *(int *)(DAT_006133a8 + 0x613368) = iVar2 + 1;
      if (iVar2 < 3) {
        Logger::Info((char *)**(undefined4 **)(DAT_006133ac + 0x61337a),DAT_006133b0 + 0x613382);
                    /* WARNING: Could not recover jumptable at 0x0061338e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        (*UNRECOVERED_JUMPTABLE_00)(param_1);
        return;
      }
      goto LAB_00613398;
    }
  }
  UNRECOVERED_JUMPTABLE_00 = (code *)**(undefined4 **)(DAT_006133b4 + 0x613396);
LAB_00613398:
                    /* WARNING: Could not recover jumptable at 0x0061339c. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE_00)();
  return;
}


// ===== HookOf_Render2dStuff @ 006133b8

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* HookOf_Render2dStuff() */

void HookOf_Render2dStuff(void)

{
  char cVar1;
  uint uVar2;
  uint uVar3;
  longlong lVar4;
  byte bVar5;
  int iVar6;
  int *piVar7;
  undefined4 *puVar8;
  int *piVar9;
  uint uVar10;
  undefined4 uVar11;
  float *pfVar12;
  longlong *plVar13;
  float fVar14;
  byte *pbVar15;
  int iVar16;
  int iVar17;
  ImGuiIO *pIVar18;
  ImGuiWindow *pIVar19;
  ImGuiWindow *pIVar20;
  float extraout_r0;
  float extraout_r0_00;
  float fVar21;
  code *pcVar22;
  int iVar23;
  float *pfVar24;
  uint uVar25;
  undefined4 uVar26;
  undefined4 uVar27;
  char *pcVar28;
  undefined4 uVar29;
  uint uVar30;
  char *pcVar31;
  mutex *this;
  uint *puVar32;
  int iVar33;
  int *piVar34;
  undefined4 *puVar35;
  uint uVar36;
  uint uVar37;
  bool bVar38;
  bool bVar39;
  uint in_fpscr;
  undefined4 extraout_s0;
  float extraout_s0_00;
  undefined4 extraout_s1;
  float extraout_s1_00;
  float extraout_s1_01;
  float extraout_s1_02;
  float __y;
  float __y_00;
  float __y_01;
  float extraout_s1_03;
  float fVar40;
  float fVar41;
  float fVar42;
  undefined8 uVar43;
  longlong lVar44;
  undefined4 local_78;
  int local_74;
  int local_70;
  undefined4 local_6c;
  undefined4 uStack_68;
  int local_64;
  
  local_64 = **(int **)(DAT_006141ac + 0x6133d8);
  (*(code *)**(undefined4 **)(DAT_0061372c + 0x6133ce))();
  pcVar28 = (char *)(DAT_00613730 + 0x6133e4);
  if (*pcVar28 == '\0') {
    InitRenderWareFunctions();
    *pcVar28 = '\x01';
  }
  iVar6 = HUD::IsInitialised();
  iVar33 = DAT_00613740;
  if (iVar6 == 0) goto LAB_00613898;
  piVar7 = (int *)(DAT_00613734 + 0x6133fe);
  puVar8 = (undefined4 *)(DAT_00613738 + 0x613408);
  if ((*piVar7 != 0) && (piVar9 = (int *)(DAT_0061373c + 0x613412), *piVar9 != 0)) {
    puVar32 = (uint *)(DAT_00613740 + 0x613422);
    if (*puVar32 == 0 && *(int *)(DAT_00613740 + 0x613426) == 0) {
      bVar39 = false;
    }
    else {
      uVar43 = MP_NowMs();
      uVar25 = *puVar32;
      uVar10 = (int)((ulonglong)uVar43 >> 0x20) - *(int *)(iVar33 + 0x613426);
      uVar37 = (uint)((uint)uVar43 < uVar25);
      bVar39 = uVar10 == uVar37 && (uint)(3999 < (uint)uVar43 - uVar25) <= uVar10 - uVar37;
    }
    iVar33 = 0;
    pcVar28 = (char *)(DAT_00613748 + 0x61345c);
    piVar34 = (int *)(DAT_00613744 + 0x613458);
    do {
      uVar10 = (*(code *)*piVar7)(iVar33,0);
      local_70 = *piVar34;
      local_74 = piVar34[1];
      uVar37 = uVar10 & 0xfffffffe;
      local_78 = 0;
      if (uVar37 == 2) {
        (*(code *)*piVar9)(iVar33,&local_70,&local_74,&local_78);
        FUN_0061463c(&local_70,&local_74);
        if (!bVar39) {
          if ((char)piVar34[-1] == '\0') {
            if (*pcVar28 == '\0') {
              iVar6 = DAT_0061374c + 0x613504;
              pcVar31 = *(char **)*puVar8;
              *pcVar28 = '\x01';
              Logger::Info(pcVar31,iVar6,iVar33,local_70,local_74);
            }
            uVar11 = 2;
          }
          else {
            iVar6 = *piVar34;
            bVar38 = local_70 == iVar6;
            if (bVar38) {
              iVar6 = piVar34[1];
            }
            if (bVar38 && local_74 == iVar6) goto LAB_0061346c;
            uVar11 = 3;
          }
LAB_0061351e:
          FUN_0061478c(uVar11,iVar33,local_70,local_74);
        }
      }
      else {
        if (!bVar39) {
          uVar10 = (uint)*(byte *)(piVar34 + -1);
        }
        if (!bVar39 && uVar10 == 1) {
          uVar11 = 1;
          goto LAB_0061351e;
        }
      }
LAB_0061346c:
      iVar33 = iVar33 + 1;
      *(bool *)(piVar34 + -1) = uVar37 == 2;
      *piVar34 = local_70;
      piVar34[1] = local_74;
      piVar34 = piVar34 + 3;
    } while (iVar33 != 4);
  }
  if (*(int *)(DAT_00613750 + 0x61352c) != 0) {
    lVar44 = MP_NowMs();
    iVar33 = DAT_00613760;
    iVar6 = (int)((ulonglong)lVar44 >> 0x20);
    uVar30 = (uint)lVar44;
    ExclusiveAccess((uint *)(DAT_00613754 + 0x613544));
    uVar10 = *(uint *)(DAT_00613754 + 0x613544);
    ClearExclusiveLocal();
    DataMemoryBarrier(0x1b);
    uVar37 = iVar6 - *(int *)(DAT_00613754 + 0x613548);
    uVar25 = (uint)(uVar30 < uVar10);
    puVar32 = (uint *)(DAT_00613758 + 0x613564);
    piVar7 = (int *)(DAT_0061375c + 0x613566);
    pfVar12 = (float *)(DAT_00613760 + 0x613568);
    if (uVar37 != uVar25 || uVar37 - uVar25 < (uint)(300 < uVar30 - uVar10)) {
      uVar10 = (*(code *)**(undefined4 **)(DAT_00613764 + 0x613574))();
      DataMemoryBarrier(0x1b);
      *puVar32 = uVar10;
      DataMemoryBarrier(0x1b);
      if (uVar10 != 0) {
        DataMemoryBarrier(0x1b);
        plVar13 = (longlong *)(DAT_00613768 + 0x613590);
        do {
          ExclusiveAccess(plVar13);
          bVar39 = (bool)hasExclusiveAccess(plVar13);
        } while (!bVar39);
        *plVar13 = lVar44;
        DataMemoryBarrier(0x1b);
      }
      if (*piVar7 != 0) {
        puVar35 = *(undefined4 **)(DAT_0061376c + 0x6135aa);
        fVar14 = (float)(*(code *)*puVar35)(0);
        fVar40 = DAT_00613770;
        fVar21 = ABS(fVar14);
        *pfVar12 = fVar14;
        uVar10 = in_fpscr & 0xfffffff | (uint)(fVar21 < fVar40) << 0x1f |
                 (uint)(fVar21 == fVar40) << 0x1e;
        uVar37 = uVar10 | (uint)(NAN(fVar21) || NAN(fVar40)) << 0x1c;
        bVar5 = (byte)(uVar10 >> 0x18);
        if (!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(uVar37 >> 0x1c) & 1)) {
          DataMemoryBarrier(0x1b);
          plVar13 = (longlong *)(DAT_00613774 + 0x6135d8);
          do {
            ExclusiveAccess(plVar13);
            bVar39 = (bool)hasExclusiveAccess(plVar13);
          } while (!bVar39);
          *plVar13 = lVar44;
          DataMemoryBarrier(0x1b);
        }
        fVar14 = (float)(*(code *)*puVar35)(1);
        fVar21 = ABS(fVar14);
        *(float *)(iVar33 + 0x61356c) = fVar14;
        uVar10 = uVar37 & 0xfffffff | (uint)(fVar21 < fVar40) << 0x1f |
                 (uint)(fVar21 == fVar40) << 0x1e;
        uVar37 = uVar10 | (uint)(NAN(fVar21) || NAN(fVar40)) << 0x1c;
        bVar5 = (byte)(uVar10 >> 0x18);
        if (!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(uVar37 >> 0x1c) & 1)) {
          DataMemoryBarrier(0x1b);
          plVar13 = (longlong *)(DAT_00613778 + 0x61360e);
          do {
            ExclusiveAccess(plVar13);
            bVar39 = (bool)hasExclusiveAccess(plVar13);
          } while (!bVar39);
          *plVar13 = lVar44;
          DataMemoryBarrier(0x1b);
        }
        fVar14 = (float)(*(code *)*puVar35)(2);
        fVar21 = ABS(fVar14);
        *(float *)(iVar33 + 0x613570) = fVar14;
        uVar10 = uVar37 & 0xfffffff | (uint)(fVar21 < fVar40) << 0x1f |
                 (uint)(fVar21 == fVar40) << 0x1e;
        uVar37 = uVar10 | (uint)(NAN(fVar21) || NAN(fVar40)) << 0x1c;
        bVar5 = (byte)(uVar10 >> 0x18);
        if (!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(uVar37 >> 0x1c) & 1)) {
          DataMemoryBarrier(0x1b);
          plVar13 = (longlong *)(DAT_0061377c + 0x613644);
          do {
            ExclusiveAccess(plVar13);
            bVar39 = (bool)hasExclusiveAccess(plVar13);
          } while (!bVar39);
          *plVar13 = lVar44;
          DataMemoryBarrier(0x1b);
        }
        fVar14 = (float)(*(code *)*puVar35)(3);
        fVar21 = ABS(fVar14);
        *(float *)(iVar33 + 0x613574) = fVar14;
        uVar10 = uVar37 & 0xfffffff | (uint)(fVar21 < fVar40) << 0x1f |
                 (uint)(fVar21 == fVar40) << 0x1e;
        in_fpscr = uVar10 | (uint)(NAN(fVar21) || NAN(fVar40)) << 0x1c;
        bVar5 = (byte)(uVar10 >> 0x18);
        if (!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(in_fpscr >> 0x1c) & 1)) {
          DataMemoryBarrier(0x1b);
          plVar13 = (longlong *)(DAT_00613780 + 0x61367c);
          do {
            ExclusiveAccess(plVar13);
            bVar39 = (bool)hasExclusiveAccess(plVar13);
          } while (!bVar39);
          *plVar13 = lVar44;
          DataMemoryBarrier(0x1b);
        }
      }
    }
    uVar10 = *puVar32;
    DataMemoryBarrier(0x1b);
    pbVar15 = (byte *)(DAT_00613784 + 0x61369c);
    if ((*piVar7 != 0) &&
       ((DataMemoryBarrier(0x1b), *pbVar15 != 0 ||
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00613788 + 0x6136ae) == '\x01')))) {
      puVar35 = *(undefined4 **)(DAT_0061378c + 0x6136bc);
      uVar11 = (*(code *)*puVar35)(4);
      pcVar22 = (code *)*puVar35;
      *(undefined4 *)(iVar33 + 0x613578) = uVar11;
      uVar11 = (*pcVar22)(5);
      *(undefined4 *)(iVar33 + 0x61357c) = uVar11;
    }
    plVar13 = (longlong *)(DAT_00613790 + 0x6136e0);
    fVar40 = *(float *)(iVar33 + 0x61357c);
    iVar16 = *(int *)plVar13;
    iVar23 = *(int *)(DAT_00613790 + 0x6136e4);
    lVar4 = *plVar13;
    uVar36 = *(uint *)(DAT_00613794 + 0x6136e2);
    uVar25 = in_fpscr & 0xfffffff | (uint)(fVar40 < 0.5) << 0x1f | (uint)(fVar40 == 0.5) << 0x1e;
    uVar37 = uVar25 | (uint)NAN(fVar40) << 0x1c;
    if (0.5 < *(float *)(iVar33 + 0x613578)) {
      uVar10 = uVar10 | 0x4000;
    }
    bVar5 = (byte)(uVar25 >> 0x18);
    if (!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(uVar37 >> 0x1c) & 1)) {
      uVar10 = uVar10 | 0x8000;
    }
    *(uint *)(DAT_00613794 + 0x6136e2) = uVar10;
    if (iVar16 != 0 || iVar23 != 0) {
      FUN_009d04a8((int)(lVar44 - lVar4),(int)((ulonglong)(lVar44 - lVar4) >> 0x20));
    }
    pcVar28 = (char *)(DAT_00613a48 + 0x6137ae);
    uVar37 = uVar37 & 0xfffffff;
    cVar1 = *pcVar28;
    *plVar13 = lVar44;
    iVar16 = DAT_00613a50;
    if ((uVar10 != 0) && (cVar1 == '\0')) {
      *pcVar28 = '\x01';
      Logger::Info(*(char **)*puVar8,iVar16 + 0x6137d8,uVar10);
    }
    iVar23 = Game::ScreenWidth();
    iVar17 = Game::ScreenHeight();
    iVar16 = iVar23;
    if (9 < iVar23) {
      iVar16 = iVar17;
    }
    if (9 < iVar16) {
      uVar25 = uVar10 & ~uVar36;
      iVar16 = MP::SpawnPickerShown();
      if ((iVar16 != 0) && (DataMemoryBarrier(0x1b), *(char *)(DAT_00613a78 + 0x613964) == '\0')) {
        if ((int)(uVar25 << 0x15) < 0) {
          MP::SpawnStep(-1);
        }
        if ((int)(uVar25 << 0x14) < 0) {
          MP::SpawnStep(1);
        }
        if ((uVar25 & 1) != 0) {
          MP::SpawnConfirm();
        }
      }
      iVar16 = DAT_00613a7c;
      plVar13 = (longlong *)(DAT_00613a7c + 0x613992);
      if ((int)(uVar25 << 0x1a) < 0) {
        *plVar13 = lVar44;
        lVar4 = lVar44;
      }
      else {
        lVar4 = *plVar13;
      }
      uVar36 = uVar36 & ~uVar10;
      uVar2 = iVar6 - (int)((ulonglong)lVar4 >> 0x20);
      uVar3 = (uint)(uVar30 < (uint)lVar4);
      pcVar28 = (char *)(DAT_00613a80 + 0x6139b6);
      if ((((int)(uVar10 << 0x1a) < 0) && (lVar4 != 0)) &&
         (uVar2 != uVar3 || uVar2 - uVar3 < (uint)(600 < uVar30 - (uint)lVar4))) {
        *(undefined4 *)plVar13 = 0;
        *(undefined4 *)(iVar16 + 0x613996) = 0;
        iVar6 = FloatMenu::IsOpen();
        if (iVar6 == 0) {
          FloatMenu::Toggle();
        }
        DataMemoryBarrier(0x1b);
        *(undefined1 *)(DAT_00613a84 + 0x6139ee) = 0;
        DataMemoryBarrier(0x1b);
        DataMemoryBarrier(0x1b);
        *pbVar15 = 1;
        DataMemoryBarrier(0x1b);
        lVar4 = *plVar13;
        *pcVar28 = '\0';
      }
      if (((int)(uVar36 << 0x1a) < 0) && (lVar4 != 0)) {
        *(undefined4 *)plVar13 = 0;
        *(undefined4 *)(iVar16 + 0x613996) = 0;
        *pcVar28 = '\0';
        DataMemoryBarrier(0x1b);
        if (*pbVar15 == 1) {
          DataMemoryBarrier(0x1b);
          *pbVar15 = 0;
          DataMemoryBarrier(0x1b);
          bVar5 = 1;
        }
        else {
          DataMemoryBarrier(0x1b);
          bVar5 = *(byte *)(DAT_00613d30 + 0x613a8e) ^ 1;
        }
        DataMemoryBarrier(0x1b);
        *(byte *)(DAT_00613d34 + 0x613aa0) = bVar5;
        DataMemoryBarrier(0x1b);
      }
      iVar6 = FloatMenu::IsModal();
      if (((iVar6 == 0) && (iVar6 = VKeyboard::IsOpen(), iVar6 == 0)) &&
         ((iVar6 = MP::AuthScreenShown(), iVar6 == 0 && (iVar6 = FloatMenu::IsOpen(), iVar6 == 0))))
      {
        uVar30 = MP::DealerShown();
      }
      else {
        uVar30 = 1;
      }
      plVar13 = (longlong *)(DAT_00613d38 + 0x613ac8);
      ExclusiveAccess(plVar13);
      ClearExclusiveLocal();
      DataMemoryBarrier(0x1b);
      if (*(int *)plVar13 != 0 || *(int *)(DAT_00613d38 + 0x613acc) != 0) {
        ExclusiveAccess(plVar13);
        ClearExclusiveLocal();
        DataMemoryBarrier(0x1b);
        if (((uVar30 == 1 &&
              (uint)-(int)((ulonglong)(lVar44 - *plVar13) >> 0x20) <
              (uint)((uint)(lVar44 - *plVar13) < 60000)) &&
            (DataMemoryBarrier(0x1b), *(char *)(DAT_00613d3c + 0x613b06) == '\0')) &&
           (DataMemoryBarrier(0x1b), *pbVar15 == 0)) {
          DataMemoryBarrier(0x1b);
          *pbVar15 = 1;
          DataMemoryBarrier(0x1b);
          *pcVar28 = '\x01';
        }
      }
      if ((uVar30 == 0) && (*pcVar28 != '\0')) {
        DataMemoryBarrier(0x1b);
        if ((*(char *)(DAT_00613d40 + 0x613b38) != '\0') || (DataMemoryBarrier(0x1b), *pbVar15 == 1)
           ) {
          DataMemoryBarrier(0x1b);
          *(char *)(DAT_00613d40 + 0x613b38) = '\0';
          DataMemoryBarrier(0x1b);
          DataMemoryBarrier(0x1b);
          *pbVar15 = 0;
          DataMemoryBarrier(0x1b);
          *pcVar28 = '\0';
        }
      }
      puVar32 = (uint *)ImGui::GetIO();
      DataMemoryBarrier(0x1b);
      if ((uVar30 & *pbVar15) == 0) {
        fVar40 = extraout_s1_00;
        if (*(int *)(DAT_00613d68 + 0x613c76) != 0) {
          FUN_00618578(0,0);
          fVar40 = extraout_s1_01;
        }
        if ((int)(*puVar32 << 0x1e) < 0) {
          *puVar32 = *puVar32 & 0xfffffffd;
          puVar32[1] = puVar32[1] & 0xfffffffe;
        }
        DataMemoryBarrier(0x1b);
        if (*pbVar15 == 1 && uVar30 == 0) {
          DataMemoryBarrier(0x1b);
          *pbVar15 = 0;
          DataMemoryBarrier(0x1b);
        }
      }
      else {
        pcVar31 = (char *)(DAT_00613d44 + 0x613b7c);
        cVar1 = *pcVar31;
        *puVar32 = *puVar32 | 2;
        puVar32[1] = puVar32[1] | 1;
        if (cVar1 == '\x01') {
          FUN_0061478c(1,0xf,(int)*(float *)(DAT_00613d48 + 0x613b94),
                       (int)*(float *)(DAT_00613d4c + 0x613b96));
          *pcVar31 = '\0';
        }
        iVar6 = ImGui::GetCurrentContext();
        if (iVar6 == 0) {
          if ((int)(uVar25 << 0x1e) < 0) {
LAB_00613cbe:
            iVar6 = VKeyboard::IsOpen();
            if (iVar6 == 0) {
              iVar6 = MP::DealerBack();
              if (iVar6 == 0) {
                iVar6 = FloatMenu::IsOpen();
                if (iVar6 == 0) {
                  FloatMenu::CloseAll();
                }
                else {
                  FloatMenu::Toggle();
                }
              }
            }
            else {
              VKeyboard::Close();
            }
          }
        }
        else {
          pIVar19 = (ImGuiWindow *)ImGui::FindWindowByName((char *)(DAT_00613d50 + 0x613bcc));
          if ((pIVar19 == (ImGuiWindow *)0x0) || (pIVar19[0xab] != (ImGuiWindow)0x1)) {
            uVar43 = ImGui::FindWindowByName((char *)(DAT_00613d54 + 0x613bde));
            uVar30 = (uint)((ulonglong)uVar43 >> 0x20);
            pIVar19 = (ImGuiWindow *)uVar43;
            if (pIVar19 != (ImGuiWindow *)0x0) {
              uVar30 = (uint)(byte)pIVar19[0xab];
            }
            if (pIVar19 != (ImGuiWindow *)0x0 && uVar30 != 0) goto LAB_00613c3c;
            uVar43 = ImGui::FindWindowByName((char *)(DAT_00613d58 + 0x613bf2));
            uVar30 = (uint)((ulonglong)uVar43 >> 0x20);
            pIVar19 = (ImGuiWindow *)uVar43;
            if (pIVar19 != (ImGuiWindow *)0x0) {
              uVar30 = (uint)(byte)pIVar19[0xab];
            }
            if (pIVar19 != (ImGuiWindow *)0x0 && uVar30 != 0) goto LAB_00613c3c;
            uVar43 = ImGui::FindWindowByName((char *)(DAT_00613d5c + 0x613c06));
            uVar30 = (uint)((ulonglong)uVar43 >> 0x20);
            pIVar19 = (ImGuiWindow *)uVar43;
            if (pIVar19 != (ImGuiWindow *)0x0) {
              uVar30 = (uint)(byte)pIVar19[0xab];
            }
            if (pIVar19 != (ImGuiWindow *)0x0 && uVar30 != 0) goto LAB_00613c3c;
            uVar43 = ImGui::FindWindowByName((char *)(DAT_00613d60 + 0x613c1a));
            uVar30 = (uint)((ulonglong)uVar43 >> 0x20);
            pIVar19 = (ImGuiWindow *)uVar43;
            if (pIVar19 != (ImGuiWindow *)0x0) {
              uVar30 = (uint)(byte)pIVar19[0xab];
            }
            if (pIVar19 != (ImGuiWindow *)0x0 && uVar30 != 0) goto LAB_00613c3c;
            uVar43 = ImGui::FindWindowByName((char *)(DAT_00613d64 + 0x613c2e));
            uVar30 = (uint)((ulonglong)uVar43 >> 0x20);
            pIVar19 = (ImGuiWindow *)uVar43;
            if (pIVar19 != (ImGuiWindow *)0x0) {
              uVar30 = (uint)(byte)pIVar19[0xab];
            }
            if (pIVar19 != (ImGuiWindow *)0x0 && uVar30 != 0) goto LAB_00613c3c;
          }
          else {
LAB_00613c3c:
            if ((*(int *)(iVar6 + 0x47d8) == 0) ||
               (*(ImGuiWindow **)(*(int *)(iVar6 + 0x47d8) + 0x300) != pIVar19)) {
              ImGui::FocusWindow(pIVar19,0);
            }
          }
          if ((int)(uVar25 << 0x1e) < 0) {
            if ((*(int *)(iVar6 + 0x3ea4) == 0) && (*(int *)(iVar6 + 0x47b0) < 1)) {
              iVar6 = *(int *)(iVar6 + 0x47d8);
              bVar5 = 0;
              if (iVar6 != 0) {
                bVar5 = *(byte *)(iVar6 + 0xf);
              }
              if (iVar6 == 0 || (bVar5 & 1) == 0) goto LAB_00613cbe;
            }
            ImGuiIO::AddKeyEvent(puVar32,0x26c,1);
          }
        }
        if ((int)(uVar36 << 0x1e) < 0) {
          ImGuiIO::AddKeyEvent(puVar32,0x26c,0);
        }
        if ((int)(uVar25 << 0x1b) < 0) {
          iVar6 = VKeyboard::IsOpen();
          if (iVar6 == 0) {
            iVar6 = MP::DealerBack();
            if (iVar6 == 0) {
              iVar6 = FloatMenu::IsOpen();
              if (iVar6 == 0) {
                FloatMenu::CloseAll();
              }
              else {
                FloatMenu::Toggle();
              }
            }
          }
          else {
            VKeyboard::Close();
          }
        }
        iVar6 = DAT_0061419c;
        if ((int)(uVar25 << 0x1d) < 0) {
          DataMemoryBarrier(0x1b);
          *pbVar15 = 0;
          DataMemoryBarrier(0x1b);
          DataMemoryBarrier(0x1b);
          *(undefined1 *)(iVar6 + 0x613e3c) = 1;
          uVar10 = 0;
          DataMemoryBarrier(0x1b);
          uVar11 = 0;
        }
        else {
          if ((uVar25 & 0xc0) != 0) {
            iVar6 = FloatMenu::IsModal();
            if (iVar6 != 0) {
              iVar6 = -1;
              if (-1 < (int)(uVar25 * 0x2000000)) {
                iVar6 = 1;
              }
              iVar6 = MP::LauncherPadPage(iVar6);
              if (iVar6 != 0) goto LAB_00613dac;
            }
            uVar11 = 0x273;
            if (-1 < (int)(uVar25 * 0x2000000)) {
              uVar11 = 0x274;
            }
            ImGuiIO::AddKeyEvent(puVar32,uVar11,1);
          }
LAB_00613dac:
          if ((int)(uVar36 << 0x19) < 0) {
            ImGuiIO::AddKeyEvent(puVar32,0x273,0);
          }
          if ((int)(uVar36 << 0x18) < 0) {
            ImGuiIO::AddKeyEvent(puVar32,0x274,0);
          }
          lVar44 = MP_NowMs();
          uVar30 = (uint)((ulonglong)lVar44 >> 0x20);
          puVar32 = (uint *)(DAT_00614198 + 0x613de4);
          if ((uVar25 & 0xc000) == 0) {
            if (((uVar10 & 0xc000) != 0) &&
               (*(uint *)(DAT_00614198 + 0x613de8) <= uVar30 &&
                (uint)((uint)lVar44 <= *puVar32) <= *(uint *)(DAT_00614198 + 0x613de8) - uVar30)) {
              iVar6 = ImGui::GetCurrentContext();
              if (iVar6 != 0) {
                pIVar19 = *(ImGuiWindow **)(iVar6 + 0x47d8);
                if (*(ImGuiWindow **)(iVar6 + 0x47d8) == (ImGuiWindow *)0x0) {
                  pIVar19 = *(ImGuiWindow **)(iVar6 + 0x3e5c);
                }
                do {
                  pIVar20 = pIVar19;
                  if (pIVar20 == (ImGuiWindow *)0x0) {
                    uVar30 = 0x15e;
                    goto LAB_00613f0e;
                  }
                  fVar40 = *(float *)(pIVar20 + 0x84);
                  uVar30 = uVar37 & 0xfffffff | (uint)(fVar40 == 0.0) << 0x1e;
                  uVar37 = uVar30 | (uint)(0.0 <= fVar40) << 0x1d;
                  if ((bool)((byte)(uVar37 >> 0x1d) & 1) && !SUB41(uVar30 >> 0x1e,0)) break;
                  bVar39 = ((byte)pIVar20[0xf] & 1) != 0;
                  pIVar19 = (ImGuiWindow *)0x0;
                  if (bVar39) {
                    pIVar19 = *(ImGuiWindow **)(pIVar20 + 0x2f8);
                  }
                } while (bVar39 && pIVar19 != (ImGuiWindow *)0x0);
                uVar30 = 0x15e;
                goto LAB_00613ec0;
              }
              uVar30 = 0x15e;
              goto LAB_00613f0e;
            }
          }
          else {
            iVar6 = ImGui::GetCurrentContext();
            if (iVar6 == 0) {
LAB_00613eb0:
              uVar30 = 0x1c2;
            }
            else {
              pIVar19 = *(ImGuiWindow **)(iVar6 + 0x47d8);
              if (*(ImGuiWindow **)(iVar6 + 0x47d8) == (ImGuiWindow *)0x0) {
                pIVar19 = *(ImGuiWindow **)(iVar6 + 0x3e5c);
              }
              do {
                pIVar20 = pIVar19;
                if (pIVar20 == (ImGuiWindow *)0x0) goto LAB_00613eb0;
                fVar40 = *(float *)(pIVar20 + 0x84);
                uVar30 = uVar37 & 0xfffffff | (uint)(fVar40 == 0.0) << 0x1e;
                uVar37 = uVar30 | (uint)(0.0 <= fVar40) << 0x1d;
                if (((bool)((byte)(uVar37 >> 0x1d) & 1) && !SUB41(uVar30 >> 0x1e,0)) ||
                   (((byte)pIVar20[0xf] & 1) == 0)) {
                  uVar30 = 0x1c2;
                  goto LAB_00613ec0;
                }
                pIVar19 = *(ImGuiWindow **)(pIVar20 + 0x2f8);
              } while (*(ImGuiWindow **)(pIVar20 + 0x2f8) != (ImGuiWindow *)0x0);
              uVar30 = 0x1c2;
LAB_00613ec0:
              uVar37 = uVar37 & 0xfffffff;
              ImGui::SetScrollY(pIVar20,fVar40);
            }
LAB_00613f0e:
            *(ulonglong *)puVar32 = lVar44 + (ulonglong)uVar30;
          }
          fVar40 = ABS(*(float *)(iVar33 + 0x613574));
          uVar30 = uVar37 & 0xfffffff | (uint)(fVar40 < 0.25) << 0x1f |
                   (uint)(fVar40 == 0.25) << 0x1e;
          uVar37 = uVar30 | (uint)NAN(fVar40) << 0x1c;
          bVar5 = (byte)(uVar30 >> 0x18);
          if ((!(bool)(bVar5 >> 6 & 1) && bVar5 >> 7 == ((byte)(uVar37 >> 0x1c) & 1)) &&
             (iVar6 = ImGui::GetCurrentContext(), iVar6 != 0)) {
            pIVar19 = *(ImGuiWindow **)(iVar6 + 0x47d8);
            uVar30 = uVar37;
            if (*(ImGuiWindow **)(iVar6 + 0x47d8) == (ImGuiWindow *)0x0) {
              pIVar19 = *(ImGuiWindow **)(iVar6 + 0x3e5c);
            }
            do {
              uVar37 = uVar30;
              pIVar20 = pIVar19;
              if (pIVar20 == (ImGuiWindow *)0x0) goto LAB_00613fbc;
              fVar40 = *(float *)(pIVar20 + 0x84);
              uVar37 = uVar37 & 0xfffffff;
              uVar2 = uVar37 | (uint)(fVar40 == 0.0) << 0x1e;
              uVar30 = uVar2 | (uint)(0.0 <= fVar40) << 0x1d;
              if ((bool)((byte)(uVar30 >> 0x1d) & 1) && !SUB41(uVar2 >> 0x1e,0)) break;
              bVar39 = ((byte)pIVar20[0xf] & 1) != 0;
              pIVar19 = (ImGuiWindow *)0x0;
              if (bVar39) {
                pIVar19 = *(ImGuiWindow **)(pIVar20 + 0x2f8);
              }
            } while (bVar39 && pIVar19 != (ImGuiWindow *)0x0);
            ImGui::SetScrollY(pIVar20,fVar40);
          }
LAB_00613fbc:
          uVar11 = 1;
        }
        FUN_00618578(uVar10,uVar11);
        fVar40 = extraout_s1_02;
      }
      iVar6 = DAT_00614378;
      DataMemoryBarrier(0x1b);
      if (*pbVar15 == 0) {
        pcVar31 = (char *)(DAT_00614368 + 0x613fd8);
        DataMemoryBarrier(0x1b);
        if (*pcVar31 == '\0') {
          pcVar28 = (char *)(DAT_0061436c + 0x6141b6);
          if (*pcVar28 == '\x01') {
            FUN_0061478c(1,0xf,(int)*(float *)(DAT_00614370 + 0x6141c4),
                         (int)*(float *)(DAT_00614374 + 0x6141c6));
            *pcVar28 = '\0';
          }
        }
        else if ((int)(uVar25 << 0x1d) < 0) {
          DataMemoryBarrier(0x1b);
          *pcVar31 = '\0';
          DataMemoryBarrier(0x1b);
          DataMemoryBarrier(0x1b);
          pcVar31 = (char *)(iVar6 + 0x614204);
          *pbVar15 = 1;
          DataMemoryBarrier(0x1b);
          if (*pcVar31 == '\x01') {
            fVar14 = *(float *)(DAT_0061437c + 0x61421a);
            fVar40 = *(float *)(DAT_00614380 + 0x61421c);
LAB_00614268:
            FUN_0061478c(1,0xf,(int)fVar14,(int)fVar40);
            *pcVar31 = '\0';
          }
        }
        else {
          fVar41 = (float)VectorSignedToFloat(iVar17,(byte)(uVar37 >> 0x16) & 3);
          pfVar12 = (float *)(DAT_00614384 + 0x613ff8);
          fVar14 = *pfVar12;
          fVar21 = (float)VectorSignedToFloat(iVar23,(byte)(uVar37 >> 0x16) & 3);
          if (fVar14 < 0.0) {
            fVar14 = fVar21 * 0.5;
            pfVar24 = (float *)(DAT_00614388 + 0x61401a);
            *pfVar12 = fVar14;
            *pfVar24 = fVar41 * 0.5;
          }
          iVar6 = DAT_0061438c;
          if ((int)(uVar25 << 0x1e) < 0) {
            DataMemoryBarrier(0x1b);
            *pcVar31 = '\0';
            DataMemoryBarrier(0x1b);
            pcVar31 = (char *)(iVar6 + 0x614246);
            cVar1 = *pcVar31;
            *pcVar28 = '\0';
            if (cVar1 == '\x01') {
              fVar40 = *(float *)(DAT_00614390 + 0x61425a);
              goto LAB_00614268;
            }
          }
          else {
            pfVar24 = (float *)(DAT_00614394 + 0x6140a4);
            fVar42 = *pfVar24;
            fVar40 = fminf(fVar21 + -1.0,fVar40);
            fmaxf(fVar40,__y);
            fVar40 = fminf(fVar41 + -1.0,__y_00);
            fmaxf(fVar40,__y_01);
            iVar6 = DAT_00614398;
            *pfVar24 = extraout_r0_00;
            pbVar15 = (byte *)(iVar6 + 0x614118);
            bVar5 = *pbVar15;
            *pfVar12 = extraout_r0;
            if ((bVar5 == 0) &&
               (0.5 < ABS(extraout_r0 - fVar14) || 0.5 < ABS(extraout_r0_00 - fVar42))) {
              FUN_006143b4(extraout_r0,0);
            }
            if ((uVar25 & 1) == 0) {
              bVar5 = *pbVar15;
              if ((bVar5 & (0.5 < ABS(extraout_r0 - fVar14) || 0.5 < ABS(extraout_r0_00 - fVar42)))
                  == 1) {
                FUN_0061478c(3,0xf,(int)*pfVar12,(int)*pfVar24);
                bVar5 = *pbVar15;
              }
            }
            else {
              FUN_0061478c(2,0xf,(int)*pfVar12,(int)*pfVar24);
              bVar5 = 1;
              *pbVar15 = 1;
            }
            if ((uVar36 & 1) != 0 && bVar5 != 0) {
              FUN_0061478c(1,0xf,(int)*pfVar12,(int)*pfVar24);
              *pbVar15 = 0;
            }
            if (0.25 < ABS(*(float *)(iVar33 + 0x613574))) {
              pIVar18 = (ImGuiIO *)ImGui::GetIO();
              ImGuiIO::AddMouseWheelEvent(pIVar18,extraout_s0_00,extraout_s1_03);
            }
          }
        }
      }
    }
  }
  iVar33 = DAT_00613a58;
  DataMemoryBarrier(0x1b);
  piVar7 = (int *)(DAT_00613a58 + 0x613802);
  if (((*(byte *)(DAT_00613a54 + 0x6137f8) & 1) == 0) &&
     (iVar16 = __cxa_guard_acquire(DAT_0061439c + 0x61431c), iVar6 = DAT_006143a4, iVar16 != 0)) {
    iVar16 = DAT_006143a0 + 0x61432e;
    *piVar7 = 0;
    *(undefined4 *)(iVar33 + 0x613806) = 0;
    *(undefined4 *)(iVar33 + 0x61380a) = 0;
    __cxa_atexit(iVar16,piVar7,iVar6 + 0x614334);
    __cxa_guard_release(DAT_006143a8 + 0x614340);
  }
  this = (mutex *)(DAT_00613a5c + 0x61380c);
  std::__ndk1::mutex::lock(this);
  iVar6 = DAT_00613a60;
  uVar26 = *(undefined4 *)(iVar33 + 0x613806);
  uVar27 = *(undefined4 *)(iVar33 + 0x61380a);
  uVar29 = *(undefined4 *)(DAT_00613a60 + 0x613820);
  uVar11 = *(undefined4 *)(DAT_00613a60 + 0x613824);
  iVar16 = *(int *)(DAT_00613a60 + 0x61381c);
  *(int *)(DAT_00613a60 + 0x61381c) = *piVar7;
  *(undefined4 *)(iVar6 + 0x613820) = uVar26;
  *(undefined4 *)(iVar6 + 0x613824) = uVar27;
  *piVar7 = iVar16;
  *(undefined4 *)(iVar33 + 0x613806) = uVar29;
  *(undefined4 *)(iVar33 + 0x61380a) = uVar11;
  std::__ndk1::mutex::unlock(this);
  if (*piVar7 != *(int *)(iVar33 + 0x613806)) {
    pIVar18 = (ImGuiIO *)ImGui::GetIO();
    uVar43 = CONCAT44(extraout_s1,extraout_s0);
    puVar35 = (undefined4 *)*piVar7;
    puVar8 = *(undefined4 **)(iVar33 + 0x613806);
    if (puVar35 != puVar8) {
      do {
        switch(*puVar35) {
        case 0:
          break;
        case 1:
          bVar39 = true;
          goto LAB_00613850;
        case 2:
          bVar39 = false;
LAB_00613850:
          uVar43 = ImGuiIO::AddMouseButtonEvent(pIVar18,0,bVar39);
          goto switchD_00613860_default;
        case 3:
          break;
        default:
          goto switchD_00613860_default;
        }
        uVar43 = ImGuiIO::AddMousePosEvent(pIVar18,(float)uVar43,(float)((ulonglong)uVar43 >> 0x20))
        ;
switchD_00613860_default:
        puVar35 = puVar35 + 3;
      } while (puVar35 != puVar8);
      puVar35 = (undefined4 *)*piVar7;
    }
    *(undefined4 **)(iVar33 + 0x613806) = puVar35;
  }
LAB_00613898:
  HUD::Render();
  piVar7 = (int *)(DAT_00613a64 + 0x6138a2);
  iVar33 = *piVar7;
  DataMemoryBarrier(0x1b);
  if (0 < iVar33) {
    ExclusiveAccess(piVar7);
    if (*piVar7 == iVar33) {
      DataMemoryBarrier(0x1b);
      do {
        bVar39 = (bool)hasExclusiveAccess(piVar7);
        if (bVar39) {
          *piVar7 = iVar33 + -1;
          bVar39 = true;
          goto LAB_006138d0;
        }
        ExclusiveAccess(piVar7);
      } while (*piVar7 == iVar33);
    }
    bVar39 = false;
    ClearExclusiveLocal();
LAB_006138d0:
    DataMemoryBarrier(0x1b);
    if ((iVar33 == 1) && (bVar39)) {
      std::__ndk1::mutex::lock((mutex *)(DAT_00613a68 + 0x6138e0));
      iVar33 = DAT_00613a6c;
      iVar6 = *(int *)(DAT_00613a6c + 0x6138f0);
      if (0x200 < (uint)((*(int *)(DAT_00613a6c + 0x6138f4) - iVar6 >> 2) * -0x55555555)) {
        iVar16 = *(int *)(DAT_00613a6c + 0x6138f4) - (iVar6 + 0xc00);
        if (iVar16 != 0) {
          __aeabi_memmove4(iVar6,iVar6 + 0xc00,iVar16);
        }
        *(int *)(iVar33 + 0x6138f4) = iVar6 + iVar16;
      }
      local_6c = 0;
      uStack_68 = 0;
      local_70 = 3;
      FUN_006184a4(DAT_00613a70 + 0x613920,&local_70);
      std::__ndk1::mutex::unlock((mutex *)(DAT_00613a74 + 0x61392c));
    }
  }
  if (**(int **)(DAT_006143ac + 0x613938) != local_64) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ===== HookOf_LS_RenderSplash @ 00614474

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* HookOf_LS_RenderSplash() */

void HookOf_LS_RenderSplash(void)

{
  int iVar1;
  undefined1 *puVar2;
  char *pcVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  
  puVar2 = (undefined1 *)(DAT_006144fc + 0x614480);
  *puVar2 = 0;
  iVar1 = MP::MenuMpLoading();
  if ((iVar1 != 0) && (iVar1 = MP::PCMenuLoadingActive(), iVar1 == 0)) {
    pcVar3 = (char *)(DAT_00614500 + 0x614492);
    if (*pcVar3 == '\0') {
      InitRenderWareFunctions();
      *pcVar3 = '\x01';
    }
    fVar5 = DAT_006144f4;
    if (*(float **)(DAT_00614504 + 0x6144a6) != (float *)0x0) {
      fVar5 = **(float **)(DAT_00614504 + 0x6144a6);
    }
    fVar6 = DAT_006144f4;
    if (fVar5 <= DAT_006144f8) {
      fVar6 = fVar5;
    }
    fVar4 = DAT_006144f4;
    if (0.0 <= fVar5) {
      fVar4 = fVar6;
    }
    iVar1 = HUD::RenderLoading(fVar4);
    if (iVar1 != 0) {
      *puVar2 = 1;
      return;
    }
  }
                    /* WARNING: Could not recover jumptable at 0x006144f2. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)**(undefined4 **)(DAT_00614508 + 0x6144ec))();
  return;
}


// ===== HookOf_MobileMenu_Update @ 0061450c

/* HookOf_MobileMenu_Update(void*) */

void HookOf_MobileMenu_Update(void *param_1)

{
  (*(code *)**(undefined4 **)(DAT_00614524 + 0x614516))(param_1);
                    /* WARNING: Could not recover jumptable at 0x009d4818. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)PTR_MenuMpMenuTick_009f77c4)();
  return;
}


// ===== HookOf_LS_RenderLoadingBar @ 00614528

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* HookOf_LS_RenderLoadingBar() */

void HookOf_LS_RenderLoadingBar(void)

{
  int iVar1;
  
  if ((*(char *)(DAT_00614550 + 0x614532) == '\x01') && (iVar1 = MP::MenuMpLoading(), iVar1 != 0)) {
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x0061454c. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)**(undefined4 **)(DAT_00614554 + 0x614546))();
  return;
}


// ===== HookOf_AND_TouchEvent @ 00614558

/* HookOf_AND_TouchEvent(int, int, int, int) */

void HookOf_AND_TouchEvent(int param_1,int param_2,int param_3,int param_4)

{
  int iVar1;
  int iVar2;
  int iVar3;
  undefined4 *puVar4;
  int iVar5;
  undefined8 uVar6;
  int local_2c;
  int iStack_28;
  int local_24;
  
  local_24 = **(int **)(DAT_0061461c + 0x61456e);
  local_2c = param_4;
  iStack_28 = param_3;
  ScreenGuard::OnUiThread();
  iVar3 = HUD::IsInitialised();
  if (((uint)param_2 < 0x10) && (iVar3 != 0)) {
    uVar6 = MP_NowMs();
    *(undefined8 *)(DAT_00614620 + 0x614590) = uVar6;
    FUN_0061463c(&iStack_28,&local_2c);
    iVar2 = iStack_28;
    iVar1 = local_2c;
    iVar3 = DAT_0061462c;
    iVar5 = *(int *)(DAT_00614624 + 0x6145a4);
    if (iVar5 < 8) {
      puVar4 = (undefined4 *)(DAT_00614628 + 0x6145b2);
      *(int *)(DAT_00614624 + 0x6145a4) = iVar5 + 1;
      Logger::Info(*(char **)*puVar4,iVar3 + 0x6145b6,param_1,param_2);
    }
    iVar3 = FUN_0061478c(param_1,param_2,iVar2,iVar1);
    if (iVar3 != 0) {
      if (**(int **)(DAT_00614638 + 0x6145de) == local_24) {
        return;
      }
      goto LAB_006145ec;
    }
  }
  if (**(int **)(DAT_00614634 + 0x614602) == local_24) {
                    /* WARNING: Could not recover jumptable at 0x0061461a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    (*(code *)**(undefined4 **)(DAT_00614630 + 0x6145f6))(param_1,param_2,param_3,param_4);
    return;
  }
LAB_006145ec:
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}


// ===== HookOf_TI_IsTouched @ 00614d44

/* HookOf_TI_IsTouched(int, void*, int) */

undefined4 HookOf_TI_IsTouched(int param_1,void *param_2,int param_3)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00614eac, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00614e9c + 0x614d7e);
    uVar9 = *(ulonglong *)(DAT_00614e9c + 0x614d86);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00614ea0 + 0x614e44) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00614ea4 + 0x614e52) != '\x01')))) {
      puVar7 = (uint *)(DAT_00614eac + 0x614e62);
      if ((*puVar7 == 0 && *(int *)(DAT_00614eac + 0x614e66) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x614e66),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00614e98. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00614eb0 + 0x614e8a))(param_1,param_2,param_3);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00614ea8 + 0x614d64) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsReleased @ 00614eb4

/* HookOf_TI_IsReleased(int, void*, int) */

undefined4 HookOf_TI_IsReleased(int param_1,void *param_2,int param_3)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_0061501c, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_0061500c + 0x614eee);
    uVar9 = *(ulonglong *)(DAT_0061500c + 0x614ef6);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615010 + 0x614fb4) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615014 + 0x614fc2) != '\x01')))) {
      puVar7 = (uint *)(DAT_0061501c + 0x614fd2);
      if ((*puVar7 == 0 && *(int *)(DAT_0061501c + 0x614fd6) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x614fd6),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615008. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615020 + 0x614ffa))(param_1,param_2,param_3);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615018 + 0x614ed4) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsDoubleTapped @ 00615024

/* HookOf_TI_IsDoubleTapped(int, bool, int) */

undefined4 HookOf_TI_IsDoubleTapped(int param_1,bool param_2,int param_3)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_0061518c, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_0061517c + 0x61505e);
    uVar9 = *(ulonglong *)(DAT_0061517c + 0x615066);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615180 + 0x615124) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615184 + 0x615132) != '\x01')))) {
      puVar7 = (uint *)(DAT_0061518c + 0x615142);
      if ((*puVar7 == 0 && *(int *)(DAT_0061518c + 0x615146) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x615146),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615178. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615190 + 0x61516a))(param_1,param_2,param_3);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615188 + 0x615044) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsHeldDown @ 00615194

/* HookOf_TI_IsHeldDown(int, int) */

undefined4 HookOf_TI_IsHeldDown(int param_1,int param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_006152f8, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_006152e8 + 0x6151cc);
    uVar9 = *(ulonglong *)(DAT_006152e8 + 0x6151d4);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_006152ec + 0x615292) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_006152f0 + 0x6152a0) != '\x01')))) {
      puVar7 = (uint *)(DAT_006152f8 + 0x6152b0);
      if ((*puVar7 == 0 && *(int *)(DAT_006152f8 + 0x6152b4) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x6152b4),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x006152e4. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_006152fc + 0x6152d6))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_006152f4 + 0x6151b2) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsSwipedLeft @ 00615300

/* HookOf_TI_IsSwipedLeft(int, int) */

undefined4 HookOf_TI_IsSwipedLeft(int param_1,int param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615464, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00615454 + 0x615338);
    uVar9 = *(ulonglong *)(DAT_00615454 + 0x615340);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615458 + 0x6153fe) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_0061545c + 0x61540c) != '\x01')))) {
      puVar7 = (uint *)(DAT_00615464 + 0x61541c);
      if ((*puVar7 == 0 && *(int *)(DAT_00615464 + 0x615420) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x615420),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615450. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615468 + 0x615442))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615460 + 0x61531e) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsSwipedRight @ 0061546c

/* HookOf_TI_IsSwipedRight(int, int) */

undefined4 HookOf_TI_IsSwipedRight(int param_1,int param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_006155d0, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_006155c0 + 0x6154a4);
    uVar9 = *(ulonglong *)(DAT_006155c0 + 0x6154ac);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_006155c4 + 0x61556a) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_006155c8 + 0x615578) != '\x01')))) {
      puVar7 = (uint *)(DAT_006155d0 + 0x615588);
      if ((*puVar7 == 0 && *(int *)(DAT_006155d0 + 0x61558c) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x61558c),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x006155bc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_006155d4 + 0x6155ae))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_006155cc + 0x61548a) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsSwipedUp @ 006155d8

/* HookOf_TI_IsSwipedUp(int, int) */

undefined4 HookOf_TI_IsSwipedUp(int param_1,int param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_0061573c, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_0061572c + 0x615610);
    uVar9 = *(ulonglong *)(DAT_0061572c + 0x615618);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615730 + 0x6156d6) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615734 + 0x6156e4) != '\x01')))) {
      puVar7 = (uint *)(DAT_0061573c + 0x6156f4);
      if ((*puVar7 == 0 && *(int *)(DAT_0061573c + 0x6156f8) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x6156f8),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615728. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615740 + 0x61571a))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615738 + 0x6155f6) = uVar10;
  return 0;
}


// ===== HookOf_TI_IsSwipedDown @ 00615744

/* HookOf_TI_IsSwipedDown(int, int) */

undefined4 HookOf_TI_IsSwipedDown(int param_1,int param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_006158a8, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00615898 + 0x61577c);
    uVar9 = *(ulonglong *)(DAT_00615898 + 0x615784);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_0061589c + 0x615842) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_006158a0 + 0x615850) != '\x01')))) {
      puVar7 = (uint *)(DAT_006158a8 + 0x615860);
      if ((*puVar7 == 0 && *(int *)(DAT_006158a8 + 0x615864) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x615864),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615894. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_006158ac + 0x615886))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_006158a4 + 0x615762) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsTouched @ 006158b0

/* HookOf_Widget_IsTouched(void*, void*) */

undefined4 HookOf_Widget_IsTouched(void *param_1,void *param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615a14, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00615a04 + 0x6158e8);
    uVar9 = *(ulonglong *)(DAT_00615a04 + 0x6158f0);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615a08 + 0x6159ae) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615a0c + 0x6159bc) != '\x01')))) {
      puVar7 = (uint *)(DAT_00615a14 + 0x6159cc);
      if ((*puVar7 == 0 && *(int *)(DAT_00615a14 + 0x6159d0) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x6159d0),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615a00. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615a18 + 0x6159f2))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615a10 + 0x6158ce) = uVar10;
  return 0;
}


// ===== HookOf_Pad_CycleWeapL @ 00615a1c

/* HookOf_Pad_CycleWeapL(void*) */

undefined4 HookOf_Pad_CycleWeapL(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  undefined8 uVar8;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615a74, iVar4 == 0)) {
    puVar7 = (uint *)(DAT_00615a74 + 0x615a44);
    if ((*puVar7 == 0 && *(int *)(DAT_00615a74 + 0x615a48) == 0) ||
       (uVar8 = MP_NowMs(), uVar6 = *puVar7,
       uVar1 = (int)((ulonglong)uVar8 >> 0x20) - *(int *)(iVar3 + 0x615a48),
       uVar2 = (uint)((uint)uVar8 < uVar6),
       uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar8 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615a6e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar5 = (*(code *)**(undefined4 **)(DAT_00615a78 + 0x615a66))(param_1);
      return uVar5;
    }
  }
  else {
    uVar8 = MP_NowMs();
    *(undefined8 *)(DAT_00615a70 + 0x615a38) = uVar8;
  }
  return 0;
}


// ===== HookOf_Pad_CycleWeapR @ 00615a7c

/* HookOf_Pad_CycleWeapR(void*) */

undefined4 HookOf_Pad_CycleWeapR(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  undefined8 uVar8;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615ad4, iVar4 == 0)) {
    puVar7 = (uint *)(DAT_00615ad4 + 0x615aa4);
    if ((*puVar7 == 0 && *(int *)(DAT_00615ad4 + 0x615aa8) == 0) ||
       (uVar8 = MP_NowMs(), uVar6 = *puVar7,
       uVar1 = (int)((ulonglong)uVar8 >> 0x20) - *(int *)(iVar3 + 0x615aa8),
       uVar2 = (uint)((uint)uVar8 < uVar6),
       uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar8 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615ace. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar5 = (*(code *)**(undefined4 **)(DAT_00615ad8 + 0x615ac6))(param_1);
      return uVar5;
    }
  }
  else {
    uVar8 = MP_NowMs();
    *(undefined8 *)(DAT_00615ad0 + 0x615a98) = uVar8;
  }
  return 0;
}


// ===== HookOf_Hid_JustPressed @ 00615adc

/* HookOf_Hid_JustPressed(int) */

undefined4 HookOf_Hid_JustPressed(int param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  undefined8 uVar8;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615b34, iVar4 == 0)) {
    puVar7 = (uint *)(DAT_00615b34 + 0x615b04);
    if ((*puVar7 == 0 && *(int *)(DAT_00615b34 + 0x615b08) == 0) ||
       (uVar8 = MP_NowMs(), uVar6 = *puVar7,
       uVar1 = (int)((ulonglong)uVar8 >> 0x20) - *(int *)(iVar3 + 0x615b08),
       uVar2 = (uint)((uint)uVar8 < uVar6),
       uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar8 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615b2e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar5 = (*(code *)**(undefined4 **)(DAT_00615b38 + 0x615b26))(param_1);
      return uVar5;
    }
  }
  else {
    uVar8 = MP_NowMs();
    *(undefined8 *)(DAT_00615b30 + 0x615af8) = uVar8;
  }
  return 0;
}


// ===== HookOf_Hid_Released @ 00615b3c

/* HookOf_Hid_Released(int) */

undefined4 HookOf_Hid_Released(int param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  undefined8 uVar8;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615b94, iVar4 == 0)) {
    puVar7 = (uint *)(DAT_00615b94 + 0x615b64);
    if ((*puVar7 == 0 && *(int *)(DAT_00615b94 + 0x615b68) == 0) ||
       (uVar8 = MP_NowMs(), uVar6 = *puVar7,
       uVar1 = (int)((ulonglong)uVar8 >> 0x20) - *(int *)(iVar3 + 0x615b68),
       uVar2 = (uint)((uint)uVar8 < uVar6),
       uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar8 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615b8e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar5 = (*(code *)**(undefined4 **)(DAT_00615b98 + 0x615b86))(param_1);
      return uVar5;
    }
  }
  else {
    uVar8 = MP_NowMs();
    *(undefined8 *)(DAT_00615b90 + 0x615b58) = uVar8;
  }
  return 0;
}


// ===== HookOf_Hid_DoubleTapped @ 00615b9c

/* HookOf_Hid_DoubleTapped(int) */

undefined4 HookOf_Hid_DoubleTapped(int param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  undefined8 uVar8;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615bf4, iVar4 == 0)) {
    puVar7 = (uint *)(DAT_00615bf4 + 0x615bc4);
    if ((*puVar7 == 0 && *(int *)(DAT_00615bf4 + 0x615bc8) == 0) ||
       (uVar8 = MP_NowMs(), uVar6 = *puVar7,
       uVar1 = (int)((ulonglong)uVar8 >> 0x20) - *(int *)(iVar3 + 0x615bc8),
       uVar2 = (uint)((uint)uVar8 < uVar6),
       uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar8 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615bee. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar5 = (*(code *)**(undefined4 **)(DAT_00615bf8 + 0x615be6))(param_1);
      return uVar5;
    }
  }
  else {
    uVar8 = MP_NowMs();
    *(undefined8 *)(DAT_00615bf0 + 0x615bb8) = uVar8;
  }
  return 0;
}


// ===== HookOf_Hid_Pressed @ 00615bfc

/* HookOf_Hid_Pressed(int, float*) */

undefined4 HookOf_Hid_Pressed(int param_1,float *param_2)

{
  ulonglong uVar1;
  int iVar2;
  undefined4 uVar3;
  longlong *plVar4;
  undefined8 uVar5;
  longlong lVar6;
  
  iVar2 = VKeyboard::IsOpen();
  if ((iVar2 == 0) && (iVar2 = FloatMenu::IsModal(), iVar2 == 0)) {
    plVar4 = (longlong *)(DAT_00615c78 + 0x615c36);
    if ((*(int *)plVar4 == 0 && *(int *)(DAT_00615c78 + 0x615c3a) == 0) ||
       (lVar6 = MP_NowMs(), uVar1 = lVar6 - *plVar4,
       (uint)(((uint)((uVar1 & 0x100000000) != 0) << 0x1f | (uint)uVar1 >> 1) < 0xaf) <=
       -(uint)(uVar1 >> 0x21))) {
                    /* WARNING: Could not recover jumptable at 0x00615c72. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar3 = (*(code *)**(undefined4 **)(DAT_00615c7c + 0x615c66))(param_1,param_2);
      return uVar3;
    }
  }
  else {
    uVar5 = MP_NowMs();
    *(undefined8 *)(DAT_00615c74 + 0x615c1e) = uVar5;
  }
  if (param_2 != (float *)0x0) {
    *param_2 = 0.0;
  }
  return 0;
}


// ===== HookOf_Widget_IsReleased @ 00615c80

/* HookOf_Widget_IsReleased(void*, void*) */

undefined4 HookOf_Widget_IsReleased(void *param_1,void *param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615de4, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00615dd4 + 0x615cb8);
    uVar9 = *(ulonglong *)(DAT_00615dd4 + 0x615cc0);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615dd8 + 0x615d7e) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615ddc + 0x615d8c) != '\x01')))) {
      puVar7 = (uint *)(DAT_00615de4 + 0x615d9c);
      if ((*puVar7 == 0 && *(int *)(DAT_00615de4 + 0x615da0) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x615da0),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615dd0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615de8 + 0x615dc2))(param_1,param_2);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615de0 + 0x615c9e) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsDoubleTapped @ 00615dec

/* HookOf_Widget_IsDoubleTapped(void*) */

undefined4 HookOf_Widget_IsDoubleTapped(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00615f4c, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00615f3c + 0x615e22);
    uVar9 = *(ulonglong *)(DAT_00615f3c + 0x615e2a);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00615f40 + 0x615ee8) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00615f44 + 0x615ef6) != '\x01')))) {
      puVar7 = (uint *)(DAT_00615f4c + 0x615f06);
      if ((*puVar7 == 0 && *(int *)(DAT_00615f4c + 0x615f0a) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x615f0a),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00615f38. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00615f50 + 0x615f2a))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00615f48 + 0x615e08) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsHeldDown @ 00615f54

/* HookOf_Widget_IsHeldDown(void*) */

undefined4 HookOf_Widget_IsHeldDown(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_006160b4, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_006160a4 + 0x615f8a);
    uVar9 = *(ulonglong *)(DAT_006160a4 + 0x615f92);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_006160a8 + 0x616050) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_006160ac + 0x61605e) != '\x01')))) {
      puVar7 = (uint *)(DAT_006160b4 + 0x61606e);
      if ((*puVar7 == 0 && *(int *)(DAT_006160b4 + 0x616072) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x616072),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x006160a0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_006160b8 + 0x616092))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_006160b0 + 0x615f70) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsSwipedLeft @ 006160bc

/* HookOf_Widget_IsSwipedLeft(void*) */

undefined4 HookOf_Widget_IsSwipedLeft(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_0061621c, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_0061620c + 0x6160f2);
    uVar9 = *(ulonglong *)(DAT_0061620c + 0x6160fa);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00616210 + 0x6161b8) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_00616214 + 0x6161c6) != '\x01')))) {
      puVar7 = (uint *)(DAT_0061621c + 0x6161d6);
      if ((*puVar7 == 0 && *(int *)(DAT_0061621c + 0x6161da) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x6161da),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00616208. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00616220 + 0x6161fa))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00616218 + 0x6160d8) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsSwipedRight @ 00616224

/* HookOf_Widget_IsSwipedRight(void*) */

undefined4 HookOf_Widget_IsSwipedRight(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00616384, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00616374 + 0x61625a);
    uVar9 = *(ulonglong *)(DAT_00616374 + 0x616262);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00616378 + 0x616320) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_0061637c + 0x61632e) != '\x01')))) {
      puVar7 = (uint *)(DAT_00616384 + 0x61633e);
      if ((*puVar7 == 0 && *(int *)(DAT_00616384 + 0x616342) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x616342),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00616370. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00616388 + 0x616362))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00616380 + 0x616240) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsSwipedUp @ 0061638c

/* HookOf_Widget_IsSwipedUp(void*) */

undefined4 HookOf_Widget_IsSwipedUp(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_006164ec, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_006164dc + 0x6163c2);
    uVar9 = *(ulonglong *)(DAT_006164dc + 0x6163ca);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_006164e0 + 0x616488) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_006164e4 + 0x616496) != '\x01')))) {
      puVar7 = (uint *)(DAT_006164ec + 0x6164a6);
      if ((*puVar7 == 0 && *(int *)(DAT_006164ec + 0x6164aa) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x6164aa),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x006164d8. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_006164f0 + 0x6164ca))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_006164e8 + 0x6163a8) = uVar10;
  return 0;
}


// ===== HookOf_Widget_IsSwipedDown @ 006164f4

/* HookOf_Widget_IsSwipedDown(void*) */

undefined4 HookOf_Widget_IsSwipedDown(void *param_1)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  undefined4 uVar5;
  uint uVar6;
  uint *puVar7;
  ulonglong uVar8;
  ulonglong uVar9;
  undefined8 uVar10;
  
  iVar3 = VKeyboard::IsOpen();
  if ((iVar3 == 0) && (iVar4 = FloatMenu::IsModal(), iVar3 = DAT_00616654, iVar4 == 0)) {
    uVar8 = *(ulonglong *)(DAT_00616644 + 0x61652a);
    uVar9 = *(ulonglong *)(DAT_00616644 + 0x616532);
    if (((((((((((((((((uVar8 & 1) == 0 && (uVar8 & 0x100) == 0) && (uVar8 & 0x10000) == 0) &&
                    (uVar8 & 0x1000000) == 0) && (uVar8 & 0x100000000) == 0) &&
                  (uVar8 & 0x10000000000) == 0) && (uVar8 & 0x1000000000000) == 0) &&
                (uVar8 & 0x100000000000000) == 0) && (uVar9 & 1) == 0) && (uVar9 & 0x100) == 0) &&
             (uVar9 & 0x10000) == 0) && (uVar9 & 0x1000000) == 0) && (uVar9 & 0x100000000) == 0) &&
          (uVar9 & 0x10000000000) == 0) && (uVar9 & 0x1000000000000) == 0) &&
         (uVar9 & 0x100000000000000) == 0) &&
       ((DataMemoryBarrier(0x1b), *(char *)(DAT_00616648 + 0x6165f0) == '\0' &&
        (DataMemoryBarrier(0x1b), *(char *)(DAT_0061664c + 0x6165fe) != '\x01')))) {
      puVar7 = (uint *)(DAT_00616654 + 0x61660e);
      if ((*puVar7 == 0 && *(int *)(DAT_00616654 + 0x616612) == 0) ||
         (uVar10 = MP_NowMs(), uVar6 = *puVar7,
         uVar1 = (int)((ulonglong)uVar10 >> 0x20) - *(int *)(iVar3 + 0x616612),
         uVar2 = (uint)((uint)uVar10 < uVar6),
         uVar1 != uVar2 || uVar1 - uVar2 < (uint)(0x15d < (uint)uVar10 - uVar6))) {
                    /* WARNING: Could not recover jumptable at 0x00616640. Too many branches */
                    /* WARNING: Treating indirect jump as call */
        uVar5 = (*(code *)**(undefined4 **)(DAT_00616658 + 0x616632))(param_1);
        return uVar5;
      }
      return 0;
    }
  }
  uVar10 = MP_NowMs();
  *(undefined8 *)(DAT_00616650 + 0x616510) = uVar10;
  return 0;
}


// ===== HookOf_WarPadButtons @ 0061665c

/* WARNING: Unknown calling convention -- yet parameter storage is locked */
/* HookOf_WarPadButtons() */

uint HookOf_WarPadButtons(void)

{
  bool bVar1;
  uint uVar2;
  undefined8 *puVar3;
  undefined8 uVar4;
  
  uVar2 = (*(code *)**(undefined4 **)(DAT_006166d8 + 0x616666))();
  DataMemoryBarrier(0x1b);
  *(uint *)(DAT_006166dc + 0x616676) = uVar2;
  DataMemoryBarrier(0x1b);
  uVar4 = MP_NowMs();
  DataMemoryBarrier(0x1b);
  puVar3 = (undefined8 *)(DAT_006166e0 + 0x616688);
  do {
    ExclusiveAccess(puVar3);
    bVar1 = (bool)hasExclusiveAccess(puVar3);
  } while (!bVar1);
  *puVar3 = uVar4;
  DataMemoryBarrier(0x1b);
  if (uVar2 != 0) {
    uVar4 = MP_NowMs();
    DataMemoryBarrier(0x1b);
    puVar3 = (undefined8 *)(DAT_006166e4 + 0x6166a6);
    do {
      ExclusiveAccess(puVar3);
      bVar1 = (bool)hasExclusiveAccess(puVar3);
    } while (!bVar1);
    *puVar3 = uVar4;
    DataMemoryBarrier(0x1b);
  }
  DataMemoryBarrier(0x1b);
  if ((*(char *)(DAT_006166e8 + 0x6166ba) == '\0') &&
     (DataMemoryBarrier(0x1b), *(char *)(DAT_006166ec + 0x6166c6) != '\x01')) {
    return uVar2 & 0xffffffdf;
  }
  return 0;
}


// ===== HookOf_WarPadAxis @ 006166f0

/* HookOf_WarPadAxis(int) */

float HookOf_WarPadAxis(int param_1)

{
  bool bVar1;
  float fVar2;
  undefined8 *puVar3;
  undefined8 uVar4;
  
  fVar2 = (float)(*(code *)**(undefined4 **)(DAT_00616780 + 0x616700))(param_1);
  if ((uint)param_1 < 6) {
    *(float *)(DAT_00616784 + 0x616714 + param_1 * 4) = fVar2;
  }
  if (DAT_00616778 < ABS(fVar2)) {
    uVar4 = MP_NowMs();
    DataMemoryBarrier(0x1b);
    puVar3 = (undefined8 *)(DAT_00616788 + 0x61673a);
    do {
      ExclusiveAccess(puVar3);
      bVar1 = (bool)hasExclusiveAccess(puVar3);
    } while (!bVar1);
    *puVar3 = uVar4;
    DataMemoryBarrier(0x1b);
  }
  DataMemoryBarrier(0x1b);
  if (*(char *)(DAT_0061678c + 0x61674e) == '\0') {
    DataMemoryBarrier(0x1b);
    if (*(char *)(DAT_00616790 + 0x616760) == '\x01') {
      fVar2 = DAT_0061677c;
    }
  }
  else {
    fVar2 = DAT_0061677c;
  }
  return fVar2;
}


// ===== HookOf_RwCamFarClip @ 00616b3c

/* HookOf_RwCamFarClip(void*, float) */

void HookOf_RwCamFarClip(void *param_1,float param_2)

{
  float fVar1;
  float in_r1;
  float fVar2;
  
  fVar1 = (float)Game::FogRadius();
  fVar2 = in_r1;
  if (fVar1 < in_r1) {
    fVar2 = fVar1;
  }
  if (0.0 < fVar1) {
    in_r1 = fVar2;
  }
                    /* WARNING: Could not recover jumptable at 0x00616b88. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)**(undefined4 **)(DAT_00616b8c + 0x616b7c))(param_1,in_r1);
  return;
}


// ===== HookOf_RwCamFogDist @ 00616b90

/* HookOf_RwCamFogDist(void*, float) */

void HookOf_RwCamFogDist(void *param_1,float param_2)

{
  float fVar1;
  float in_r1;
  
  fVar1 = (float)Game::FogRadius();
  if (0.0 < fVar1) {
    if (fVar1 * DAT_00616bd8 < in_r1) {
      in_r1 = fVar1 * DAT_00616bd8;
    }
  }
                    /* WARNING: Could not recover jumptable at 0x00616bd6. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)**(undefined4 **)(DAT_00616bdc + 0x616bce))(param_1,in_r1);
  return;
}


// ===== HookOf_TexRLEDecompress @ 00616be0

/* HookOf_TexRLEDecompress(unsigned char*, unsigned int, unsigned char const*, unsigned int,
   unsigned int) */

void HookOf_TexRLEDecompress(uchar *param_1,uint param_2,uchar *param_3,uint param_4,uint param_5)

{
  int iVar1;
  int *piVar2;
  uint uVar3;
  uint uVar4;
  uchar *puVar5;
  bool bVar6;
  
  if (param_1 != (uchar *)0x0) {
    bVar6 = param_3 == (uchar *)0x0;
    uVar3 = 0;
    if (!bVar6) {
      uVar3 = param_4 - 0x41;
    }
    if ((bVar6 || 0xffffffbe < uVar3) && (!bVar6 && uVar3 != 0xffffffbf)) {
      if (param_2 != 0) {
        puVar5 = param_1 + param_2;
        bVar6 = false;
        do {
          while ((uint)*param_3 != (param_5 & 0xff)) {
            if ((uint)((int)puVar5 - (int)param_1) < param_4) {
              __aeabi_memcpy(param_1,param_3);
              goto LAB_00616cbe;
            }
            __aeabi_memcpy(param_1,param_3,param_4);
            param_3 = param_3 + param_4;
            param_1 = param_1 + param_4;
            if (puVar5 <= param_1) goto LAB_00616caa;
          }
          uVar3 = (uint)param_3[1];
          if (uVar3 != 0) {
            uVar4 = (int)puVar5 - (int)param_1;
            do {
              if (uVar4 < param_4) {
                if (param_1 != puVar5) {
                  __aeabi_memcpy(param_1,param_3 + 2,uVar4);
                }
                bVar6 = true;
                param_1 = puVar5;
                break;
              }
              __aeabi_memcpy(param_1,param_3 + 2,param_4);
              uVar4 = uVar4 - param_4;
              param_1 = param_1 + param_4;
              uVar3 = uVar3 - 1;
            } while (uVar3 != 0);
          }
          param_3 = param_3 + param_4 + 2;
        } while (param_1 < puVar5);
LAB_00616caa:
        if (bVar6) {
LAB_00616cbe:
          DataMemoryBarrier(0x1b);
          piVar2 = (int *)(DAT_00616d00 + 0x616cc8);
          do {
            ExclusiveAccess(piVar2);
            iVar1 = *piVar2;
            bVar6 = (bool)hasExclusiveAccess(piVar2);
          } while (!bVar6);
          *piVar2 = iVar1 + 1;
          DataMemoryBarrier(0x1b);
          if (iVar1 < 0x14) {
            (*(code *)PTR_Error_009f7818)
                      (**(undefined4 **)(DAT_00616d08 + 0x616ce4),DAT_00616d0c + 0x616ce6,param_2);
            return;
          }
        }
      }
      return;
    }
  }
                    /* WARNING: Could not recover jumptable at 0x00616c20. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)**(undefined4 **)(DAT_00616d04 + 0x616c10))(param_1,param_2,param_3,param_4);
  return;
}


// ===== MP::PCMenuLoadingActive @ 0064860c

/* MP::PCMenuLoadingActive() */

bool MP::PCMenuLoadingActive(void)

{
  int iVar1;
  
  DataMemoryBarrier(0x1b);
  if (*(int *)(PCMenuLoadingActive + DAT_00648630 + 6) == 0) {
    return false;
  }
  iVar1 = (**(code **)(*(int *)(PCMenuLoadingActive + DAT_00648630 + 6) + 4))();
  return iVar1 != 0;
}


// ===== MP::MenuMpMenuTick @ 00648740

/* MP::MenuMpMenuTick() */

uint MP::MenuMpMenuTick(void)

{
  longlong lVar1;
  uint uVar2;
  longlong *plVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  byte *pbVar7;
  undefined4 *puVar8;
  bool bVar9;
  longlong lVar10;
  
  uVar2 = (uint)*(byte *)(DAT_006487d8 + 0x64874e);
  DataMemoryBarrier(0x1b);
  if (uVar2 == 1) {
    pbVar7 = (byte *)(DAT_006487dc + 0x64875c);
    uVar2 = (uint)*pbVar7;
    DataMemoryBarrier(0x1b);
    if (uVar2 == 1) {
      plVar3 = (longlong *)(DAT_006487e0 + 0x64876a);
      ExclusiveAccess(plVar3);
      lVar1 = *plVar3;
      ClearExclusiveLocal();
      DataMemoryBarrier(0x1b);
      uVar2 = 0;
      if ((*(int *)plVar3 != 0 || *(int *)(DAT_006487e0 + 0x64876e) != 0) &&
         (uVar2 = Game::IsLoadPending(), uVar2 == 0)) {
        iVar4 = Game::IsFrontendMenu();
        uVar2 = 0;
        if (iVar4 != 0) {
          lVar10 = MP_NowMs();
          iVar4 = DAT_006487e4;
          uVar6 = (uint)((ulonglong)(lVar10 - lVar1) >> 0x20);
          bVar9 = 399 < (uint)(lVar10 - lVar1);
          uVar2 = uVar6 - !bVar9;
          if (uVar6 != 0 || uVar6 < bVar9) {
            DataMemoryBarrier(0x1b);
            *pbVar7 = 0;
            DataMemoryBarrier(0x1b);
            puVar8 = *(undefined4 **)(iVar4 + 0x6487ac);
            iVar5 = Game::ExitFrontendMenu();
            iVar4 = DAT_006487ec + 0x6487c4;
            if (iVar5 == 0) {
              iVar4 = DAT_006487e8 + 0x6487c0;
            }
            uVar2 = (*(code *)PTR_Info_009f7718)(*puVar8,DAT_006487f0 + 0x6487c6,iVar4);
            return uVar2;
          }
        }
      }
    }
  }
  return uVar2;
}


// ===== MP::MenuMpBlack @ 006487f4

/* MP::MenuMpBlack() */

undefined1 MP::MenuMpBlack(void)

{
  DataMemoryBarrier(0x1b);
  if (*(char *)(DAT_00648818 + 0x6487fe) == '\x01') {
    DataMemoryBarrier(0x1b);
    return *(undefined1 *)(DAT_0064881c + 0x648810);
  }
  return 0;
}


// ===== MP::MenuMpLoading @ 00648820

/* MP::MenuMpLoading() */

byte MP::MenuMpLoading(void)

{
  if ((MenuMpLoading[DAT_0064885c + 6] == (code)0x0) && (*(char *)(DAT_00648860 + 0x64882e) == '\0')
     ) {
    return 0;
  }
  DataMemoryBarrier(0x1b);
  if (*(char *)(DAT_00648864 + 0x648840) == '\x01') {
    DataMemoryBarrier(0x1b);
    return *(byte *)(DAT_00648868 + 0x648852) ^ 1;
  }
  return 0;
}


// ===== MP::DrawMenuLoading @ 0064886c

/* MP::DrawMenuLoading(ImDrawList*, float, bool) */

void MP::DrawMenuLoading(ImDrawList *param_1,float param_2,bool param_3)

{
  uint uVar1;
  byte bVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  int iVar6;
  float *pfVar7;
  char *pcVar8;
  float fVar9;
  void *pvVar10;
  int iVar11;
  short *psVar12;
  short *psVar13;
  uint uVar14;
  float fVar15;
  int iVar16;
  uint extraout_r1;
  short sVar17;
  int in_r2;
  undefined8 *puVar18;
  undefined4 uVar19;
  ImVec4 *__s1;
  char *__s2;
  undefined4 uVar20;
  undefined4 uVar21;
  bool bVar22;
  bool bVar23;
  uint in_fpscr;
  float extraout_s0;
  float extraout_s0_00;
  float fVar24;
  float fVar25;
  float fVar26;
  float extraout_s0_01;
  float extraout_s0_02;
  float extraout_s1;
  float extraout_s2;
  float extraout_s2_00;
  char **ppcVar27;
  float fVar28;
  float fVar29;
  float fVar30;
  undefined8 uVar31;
  float local_e0;
  float local_dc;
  float local_d8;
  float fStack_d4;
  float local_d0;
  float local_cc;
  float local_c4;
  float local_c0;
  float local_bc;
  float local_b8;
  float local_b4;
  float fStack_b0;
  float local_ac;
  float local_a8;
  int local_6c;
  
  fVar28 = (float)(uint)param_3;
  local_6c = **(int **)(DAT_00648ee4 + 0x648888);
  if (param_1 != (ImDrawList *)0x0) {
    iVar6 = ImGui::GetIO();
    in_fpscr = in_fpscr & 0xfffffff;
    local_d0 = *(float *)(iVar6 + 8);
    local_cc = *(float *)(iVar6 + 0xc);
    if (0.0 <= fVar28) {
      uVar14 = in_fpscr | (uint)(fVar28 < DAT_006489f8) << 0x1f |
               (uint)(fVar28 == DAT_006489f8) << 0x1e;
      in_fpscr = uVar14 | (uint)(NAN(fVar28) || NAN(DAT_006489f8)) << 0x1c;
      bVar2 = (byte)(uVar14 >> 0x18);
      if (!(bool)(bVar2 >> 6 & 1) && bVar2 >> 7 == ((byte)(in_fpscr >> 0x1c) & 1)) {
        fVar28 = DAT_006489f8;
      }
    }
    else {
      fVar28 = DAT_006489f4;
    }
    uVar31 = MP_NowMs();
    if (in_r2 == 0) {
      if (*(char *)(DAT_00648a04 + 0x6488f6) != '\0') {
        fVar28 = DAT_00648a0c + fVar28 * DAT_00648a08;
      }
    }
    else {
      DataMemoryBarrier(0x1b);
      fVar28 = fVar28 * DAT_006489fc;
      puVar18 = (undefined8 *)(DAT_00648a00 + 0x6488e0);
      do {
        ExclusiveAccess(puVar18);
        bVar23 = (bool)hasExclusiveAccess(puVar18);
      } while (!bVar23);
      *puVar18 = uVar31;
    }
    DataMemoryBarrier(0x1b);
    pfVar7 = (float *)(DAT_00648a10 + 0x648914);
    DataMemoryBarrier(0x1b);
    in_fpscr = in_fpscr & 0xfffffff;
    if (fVar28 < *pfVar7) {
      fVar28 = *pfVar7;
      DataMemoryBarrier(0x1b);
    }
    DataMemoryBarrier(0x1b);
    *pfVar7 = fVar28;
    DataMemoryBarrier(0x1b);
    pcVar8 = (char *)UI::Font();
    fVar9 = (float)ImGui::GetFontSize();
    local_ac = 0.0;
    local_a8 = 0.0;
    ImDrawList::AddRectFilled
              (param_1,(ImVec2 *)&local_ac,(ImVec2 *)&local_d0,0xff140b08,extraout_s0,0);
    local_d8 = 1.0;
    fStack_d4 = 1.0;
    pvVar10 = (void *)HUD::Texture((char *)(DAT_00648a14 + 0x648976),(ImVec2 *)&local_d8);
    fVar25 = extraout_s0_00;
    if (pvVar10 != (void *)0x0) {
      uVar14 = in_fpscr & 0xfffffff;
      uVar1 = uVar14 | (uint)(local_d0 == 0.0) << 0x1e;
      in_fpscr = uVar1 | (uint)(0.0 <= local_d0) << 0x1d;
      local_ac = 0.0;
      local_a8 = 0.0;
      bVar23 = SUB41(uVar1 >> 0x1e,0);
      bVar22 = (bool)((byte)(in_fpscr >> 0x1d) & 1);
      local_b4 = local_d0;
      fStack_b0 = local_cc;
      fVar24 = extraout_s2;
      if (bVar22 && !bVar23) {
        in_fpscr = uVar14 | (uint)(local_cc == 0.0) << 0x1e | (uint)(0.0 <= local_cc) << 0x1d;
        bVar2 = (byte)(in_fpscr >> 0x18);
        bVar23 = (bool)(bVar2 >> 6);
        bVar22 = (bool)(bVar2 >> 5 & 1);
        fVar24 = local_cc;
      }
      fVar25 = local_d0;
      if (bVar22 && !bVar23) {
        fVar24 = local_d0 / fVar24;
        local_b8 = 0.0;
        local_c4 = local_d8;
        local_c0 = fStack_d4;
        local_bc = 0.0;
        uVar14 = in_fpscr & 0xfffffff | (uint)(fVar24 < 2.0) << 0x1f | (uint)(fVar24 == 2.0) << 0x1e
        ;
        in_fpscr = uVar14 | (uint)NAN(fVar24) << 0x1c;
        bVar2 = (byte)(uVar14 >> 0x18);
        if ((bool)(bVar2 >> 6 & 1) || bVar2 >> 7 != ((byte)(in_fpscr >> 0x1c) & 1)) {
          local_bc = (1.0 - fVar24 * 0.5) * local_d8 * 0.5;
          local_c4 = local_d8 - local_bc;
        }
        else {
          local_b8 = (-2.0 / fVar24 + 1.0) * fStack_d4 * 0.5;
          local_c0 = fStack_d4 - local_b8;
        }
        fVar25 = (float)ImDrawList::AddImage
                                  (param_1,pvVar10,(ImVec2 *)&local_ac,(ImVec2 *)&local_b4,
                                   (ImVec2 *)&local_bc,(ImVec2 *)&local_c4,0xffffffff);
      }
    }
    local_ac = 0.0;
    local_a8 = 0.0;
    ImDrawList::AddRectFilled(param_1,(ImVec2 *)&local_ac,(ImVec2 *)&local_d0,0x820c0806,fVar25,0);
    fVar4 = local_cc;
    fVar30 = local_d0;
    uVar20 = **(undefined4 **)(param_1 + 0x2c);
    uVar21 = (*(undefined4 **)(param_1 + 0x2c))[1];
    ImDrawList::PrimReserve(param_1,0x120,0x60);
    fVar29 = DAT_00648a24;
    fVar15 = DAT_00648a20;
    fVar24 = DAT_00648a1c;
    fVar25 = DAT_00648a18;
    uVar19 = *(undefined4 *)(param_1 + 0x28);
    pfVar7 = *(float **)(param_1 + 0x34);
    iVar6 = 0;
    do {
      fVar5 = local_cc;
      fVar3 = local_d0;
      fVar26 = (float)VectorUnsignedToFloat(iVar6,(byte)(in_fpscr >> 0x16) & 3);
      fVar26 = (fVar26 * fVar25) / fVar24;
      sincosf(fVar26,(float *)fVar26,&local_dc);
      iVar6 = iVar6 + 1;
      *pfVar7 = fVar30 * 0.5 + local_e0 * fVar3 * fVar15;
      pfVar7[1] = fVar4 * 0.5 + local_dc * fVar5 * fVar15;
      iVar11 = *(int *)(param_1 + 0x34);
      *(undefined4 *)(iVar11 + 8) = uVar20;
      *(undefined4 *)(iVar11 + 0xc) = uVar21;
      iVar16 = *(int *)(param_1 + 0x34);
      *(int *)(param_1 + 0x34) = iVar16 + 0x14;
      *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) + 1;
      *(float *)(iVar16 + 0x14) = fVar30 * 0.5 + local_e0 * local_d0 * fVar29;
      *(float *)(iVar16 + 0x18) = fVar4 * 0.5 + local_dc * local_cc * fVar29;
      iVar11 = *(int *)(param_1 + 0x34);
      *(undefined4 *)(iVar16 + 0x10) = 0;
      *(undefined4 *)(iVar11 + 8) = uVar20;
      *(undefined4 *)(iVar11 + 0xc) = uVar21;
      iVar11 = *(int *)(param_1 + 0x34);
      pfVar7 = (float *)(iVar11 + 0x14);
      *(float **)(param_1 + 0x34) = pfVar7;
      *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) + 1;
      *(undefined4 *)(iVar11 + 0x10) = 0xa5000000;
    } while (iVar6 != 0x30);
    iVar6 = 2;
    psVar12 = *(short **)(param_1 + 0x38);
    do {
      sVar17 = (short)uVar19 + (short)iVar6;
      bVar23 = iVar6 == 0x60;
      psVar12[3] = sVar17 + -2;
      iVar6 = iVar6 + 2;
      psVar12[1] = sVar17 + -1;
      psVar13 = psVar12 + 6;
      *psVar12 = sVar17 + -2;
      if (bVar23) {
        sVar17 = (short)uVar19;
      }
      psVar12[5] = sVar17;
      psVar12[4] = sVar17 + 1;
      psVar12[2] = sVar17 + 1;
      local_a8 = DAT_00648ec0;
      psVar12 = psVar13;
    } while (iVar6 != 0x62);
    *(short **)(param_1 + 0x38) = psVar13;
    local_a8 = local_cc * local_a8;
    local_ac = 0.0;
    ImDrawList::AddRectFilledMultiColor
              (param_1,(ImVec2 *)&local_ac,(ImVec2 *)&local_d0,0,0,0x78000000,0x78000000);
    local_b4 = 1.0;
    fStack_b0 = 1.0;
    pvVar10 = (void *)HUD::Texture((char *)(DAT_00648ee8 + 0x648be8),(ImVec2 *)&local_b4);
    if (pvVar10 != (void *)0x0) {
      fVar25 = local_cc * 1.25;
      if (local_d0 * DAT_00648ec4 < local_cc * 1.25) {
        fVar25 = local_d0 * DAT_00648ec4;
      }
      local_c4 = 0.0;
      local_c0 = 0.0;
      local_b8 = fVar25 * DAT_00648ec8 * 0.5;
      local_ac = local_d0 * 0.5 - fVar25 * 0.5;
      local_bc = fVar25 * 0.5 + local_d0 * 0.5;
      local_a8 = local_cc * DAT_00648ec4 - local_b8;
      local_b8 = local_b8 + local_cc * DAT_00648ec4;
      ImDrawList::AddImage
                (param_1,pvVar10,(ImVec2 *)&local_ac,(ImVec2 *)&local_bc,(ImVec2 *)&local_c4,
                 (ImVec2 *)&local_b4,0xffffffff);
    }
    uVar14 = FUN_009d0e48((int)uVar31,(int)((ulonglong)uVar31 >> 0x20),3000,0);
    uVar14 = uVar14 + extraout_r1 + (uint)CARRY4(uVar14,extraout_r1);
    __s2 = (char *)(DAT_00648eec + 0x648c94);
    __s1 = *(ImVec4 **)
            (DAT_00648ef0 + 0x648c96 +
            (uVar14 - (((uint)((ulonglong)uVar14 * 0xcccccccd >> 0x20) & 0xfffffffc) + uVar14 / 5))
            * 4);
    fVar24 = (float)UI::S(extraout_s0_01);
    iVar6 = strncmp((char *)__s1,__s2,5);
    fVar25 = DAT_00648ecc;
    ppcVar27 = (char **)(fVar9 * DAT_00648ecc);
    fVar29 = (local_cc - fVar24) - fVar9 * 0.5;
    fVar15 = (float)UI::S(local_cc - fVar24);
    fVar25 = (float)ppcVar27 * fVar25;
    fVar30 = (fVar29 - fVar15) - (float)ppcVar27;
    local_bc = fVar25 + fVar24;
    local_b8 = fVar30 + (float)ppcVar27 * 0.5;
    ImDrawList::AddCircleFilled(param_1,local_bc,(uint)&local_bc,(int)fVar25);
    local_a8 = local_b8 + fVar25 * DAT_00648ed4;
    local_ac = local_bc - fVar25 * DAT_00648ed0;
    local_c0 = local_b8 + fVar25 * DAT_00648ed8;
    local_c4 = fVar25 * DAT_00648ed0 + local_bc;
    ImDrawList::AddRectFilled(param_1,(ImVec2 *)&local_ac,(ImVec2 *)&local_c4,0xffffffff,local_c4,0)
    ;
    local_a8 = local_b8 + fVar25 * DAT_00648edc;
    local_ac = local_bc;
    ImDrawList::AddCircleFilled
              (param_1,fVar25 * DAT_00648ee0,(uint)&local_ac,(int)(fVar25 * DAT_00648ee0));
    fVar15 = (float)UI::S(fVar25 + fVar25);
    uVar31 = FUN_00648f0c(ppcVar27,param_1,pcVar8,fVar25 + fVar25 + fVar24 + fVar15,fVar30,
                          0xff2b41cd,DAT_00648ef4 + 0x648dc0);
    if (iVar6 == 0) {
      __s1 = __s1 + 5;
    }
    uVar19 = 0;
    uVar20 = 0;
    uVar21 = 0;
    local_ac = fVar24;
    local_a8 = fVar29;
    ImDrawList::AddText((ImFont *)param_1,(float)uVar31,pcVar8,(uint)(fVar9 * 0.5),(char *)&local_ac
                        ,(char *)0xebaaaaaa,(float)((ulonglong)uVar31 >> 0x20),__s1);
    iVar6 = DAT_00648efc + 0x648e14;
    if (fVar28 < DAT_00648ebc) {
      iVar6 = DAT_00648ef8 + 0x648e12;
    }
    if (in_r2 != 0) {
      iVar6 = DAT_00648f00 + 0x648e22;
    }
    FUN_0063d9cc(&local_ac,0x40,0x40,DAT_00648f04 + 0x648e24,iVar6,(int)fVar28,uVar19,uVar20,uVar21)
    ;
    uVar19 = 0;
    uVar20 = 0;
    ImFont::CalcTextSizeA
              (extraout_s0_02,extraout_s1,extraout_s2_00,(char *)&local_c4,pcVar8,ppcVar27);
    param_2 = (float)FUN_00648f0c(ppcVar27,param_1,pcVar8,(local_d0 - fVar24) - local_c4,
                                  (local_cc - fVar24) - local_c0,0xf0d7d7d7,&local_ac,uVar19,uVar20)
    ;
  }
  if (**(int **)(DAT_00648f08 + 0x648ea2) != local_6c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(param_2);
  }
  return;
}


// ===== MP::DrawMenuMpBack @ 00649000

/* MP::DrawMenuMpBack(ImDrawList*) */

void MP::DrawMenuMpBack(ImDrawList *param_1)

{
  int iVar1;
  float extraout_s0;
  undefined4 local_1c;
  undefined4 uStack_18;
  int local_14;
  
  local_14 = **(int **)(DAT_00649064 + 0x64900c);
  if (((param_1 != (ImDrawList *)0x0) &&
      (DataMemoryBarrier(0x1b), *(char *)(DAT_00649068 + 0x649018) == '\x01')) &&
     (DataMemoryBarrier(0x1b), *(char *)(DAT_0064906c + 0x649026) == '\x01')) {
    local_1c = 0;
    uStack_18 = 0;
    iVar1 = ImGui::GetIO();
    ImDrawList::AddRectFilled
              (param_1,(ImVec2 *)&local_1c,(ImVec2 *)(iVar1 + 8),0xff000000,extraout_s0,0);
  }
  if (**(int **)(DAT_00649070 + 0x649056) != local_14) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ===== MP::DrawMenuMpFront @ 00649080

/* MP::DrawMenuMpFront() */

void MP::DrawMenuMpFront(void)

{
  int iVar1;
  ImDrawList *pIVar2;
  uint uVar3;
  int iVar4;
  float fVar5;
  uint in_fpscr;
  float extraout_s0;
  float fVar6;
  float extraout_s0_00;
  float extraout_s0_01;
  float fVar7;
  float extraout_s0_02;
  float extraout_s1;
  undefined8 uVar8;
  float extraout_s2;
  undefined4 uVar9;
  float fVar10;
  float local_98;
  float local_94;
  float local_90;
  float local_8c;
  float local_88;
  float local_84;
  float local_80;
  float local_7c;
  float local_78;
  float local_74;
  float local_70;
  float local_6c;
  undefined8 local_68;
  undefined8 local_60;
  float local_58;
  float local_54;
  float local_50;
  float local_4c;
  int local_44;
  
  local_44 = **(int **)(DAT_006495f0 + 0x649096);
  iVar1 = ImGui::GetIO();
  local_70 = *(float *)(iVar1 + 8);
  local_6c = *(float *)(iVar1 + 0xc);
  DataMemoryBarrier(0x1b);
  if ((*(char *)(DAT_006493d0 + 0x6490a8) == '\x01') &&
     (DataMemoryBarrier(0x1b), *(char *)(DAT_006493d4 + 0x6490ba) == '\x01')) {
    DataMemoryBarrier(0x1b);
    if (*(int *)(DAT_006493d8 + 0x6490c8) < 0) {
      DataMemoryBarrier(0x1b);
      if ((*(char *)(DAT_006495f4 + 0x6493fa) == '\x01') &&
         (iVar1 = FloatMenu::IsModal(), iVar1 == 0)) {
        fVar6 = (float)UI::S(extraout_s0_00);
        local_74 = (float)UI::S(extraout_s0_01);
        local_80 = 0.0;
        local_7c = 0.0;
        local_68 = CONCAT44((local_6c - local_74) * 0.5,(local_70 - fVar6) * 0.5);
        local_78 = fVar6;
        ImGui::SetNextWindowPos((ImVec4 *)&local_68,0,(ImVec2 *)&local_80);
        ImGui::SetNextWindowSize((ImVec2 *)&local_78,0);
        local_68 = DAT_006495a0;
        local_60 = DAT_006495a8;
        fVar6 = (float)ImGui::PushStyleColor(2,(ImVec4 *)&local_68);
        ImGui::PushStyleVar(3,fVar6);
        iVar1 = ImGui::Begin((char *)(DAT_006495f8 + 0x649494),(bool *)0x0,0x12f);
        if (iVar1 != 0) {
          ImGui::TextUnformatted((char *)(DAT_006495fc + 0x6494a2),(char *)0x0);
          local_68 = DAT_006495b0;
          local_60 = DAT_006495b8;
          fVar6 = (float)ImGui::TextColored((ImVec4 *)&local_68,(char *)(DAT_00649600 + 0x6494b2));
          uVar3 = UI::S(fVar6);
          local_68 = (ulonglong)uVar3 << 0x20;
          ImGui::Dummy((ImVec2 *)&local_68);
          fVar6 = local_78;
          iVar1 = ImGui::GetStyle();
          fVar7 = *(float *)(iVar1 + 8) + *(float *)(iVar1 + 8);
          fVar5 = (float)UI::S(fVar7);
          fVar5 = ((fVar6 - fVar7) - fVar5) * 0.5;
          fVar6 = (float)FUN_00649620();
          uVar9 = UI::S(fVar6);
          local_68 = CONCAT44(uVar9,fVar5);
          iVar1 = ImGui::Button((char *)(DAT_00649604 + 0x64950a),(ImVec2 *)&local_68);
          fVar6 = extraout_s0_02;
          if (iVar1 != 0) {
            fVar6 = (float)FloatMenu::OpenPage(0);
          }
          uVar8 = UI::S(fVar6);
          fVar6 = (float)ImGui::SameLine((float)uVar8,(float)((ulonglong)uVar8 >> 0x20));
          uVar9 = UI::S(fVar6);
          local_68 = CONCAT44(uVar9,fVar5);
          iVar1 = ImGui::Button((char *)(DAT_00649608 + 0x649540),(ImVec2 *)&local_68);
          if (iVar1 != 0) {
            DataMemoryBarrier(0x1b);
            *(undefined1 *)(DAT_0064960c + 0x649556) = 1;
            DataMemoryBarrier(0x1b);
          }
          ImGui::PopStyleVar(6);
          ImGui::PopStyleColor(0xf);
        }
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(1);
      }
    }
    else {
      uVar9 = VectorUnsignedToFloat(*(int *)(DAT_006493d8 + 0x6490c8),(byte)(in_fpscr >> 0x16) & 3);
      pIVar2 = (ImDrawList *)ImGui::GetForegroundDrawList();
      fVar6 = (float)DrawMenuLoading(pIVar2,extraout_s0,SUB41(uVar9,0));
      uVar3 = *(uint *)(DAT_006493dc + 0x6490f2);
      DataMemoryBarrier(0x1b);
      if (0 < (int)uVar3) {
        local_68 = 0;
        if (0xfe < uVar3) {
          uVar3 = 0xff;
        }
        ImDrawList::AddRectFilled
                  (pIVar2,(ImVec2 *)&local_68,(ImVec2 *)&local_70,uVar3 << 0x18,fVar6,0);
      }
    }
  }
  else if ((*(char *)(DAT_006493e0 + 0x64911e) == '\x01') &&
          (((DataMemoryBarrier(0x1b), *(char *)(DAT_006493d0 + 0x6490a8) == '\0' &&
            (iVar1 = Game::IsMainMenuShown(), iVar1 != 0)) && (iVar1 = Game::LocalPed(), iVar1 < 1))
          )) {
    local_78 = local_70 * DAT_006493e4;
    local_74 = local_6c * DAT_006493e8;
    local_80 = local_70 * DAT_006493ec;
    local_68 = 0;
    local_7c = local_6c * DAT_006493f0;
    ImGui::SetNextWindowPos((ImVec2 *)&local_80,0,(ImVec2 *)&local_68);
    ImGui::SetNextWindowSize((ImVec2 *)&local_78,0);
    local_68 = 0;
    ImGui::PushStyleVar(2,(ImVec4 *)&local_68);
    local_68 = (ulonglong)*(uint *)((undefined1  [16])0x0 + (undefined1  [16])0x4) << 0x20;
    local_60 = CONCAT44(*(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0xc),
                        *(undefined4 *)((undefined1  [16])0x0 + (undefined1  [16])0x8));
    ImGui::PushStyleColor(2,(ImVec4 *)&local_68);
    iVar1 = ImGui::Begin((char *)(DAT_00649594 + 0x6491be),(bool *)0x0,0x1af);
    if (iVar1 != 0) {
      pIVar2 = (ImDrawList *)ImGui::GetWindowDrawList();
      ImGui::GetCursorScreenPos((ImGui *)&local_88);
      iVar1 = ImGui::InvisibleButton((char *)(DAT_00649598 + 0x6491dc),(ImVec2 *)&local_78,0);
      iVar4 = ImGui::IsItemActive();
      uVar3 = 0xdc0c0c0c;
      if (iVar4 != 0) {
        uVar3 = 0xe63c3c3c;
      }
      local_54 = local_84 + local_74;
      local_58 = local_88 + local_78 * DAT_006495c4;
      local_50 = local_88;
      local_68 = CONCAT44(local_84,local_88 + local_78 * DAT_006495c0);
      local_60 = CONCAT44(local_84 + local_74 * DAT_006495c8,local_88 + local_78);
      local_4c = local_54;
      fVar6 = (float)ImDrawList::AddConvexPolyFilled(pIVar2,(ImVec2 *)&local_68,4,uVar3);
      fVar6 = (float)UI::S(fVar6);
      ImDrawList::AddPolyline(pIVar2,(ImVec2 *)&local_68,4,0xe6c8c8c8,1,fVar6);
      fVar6 = DAT_006495d0;
      fVar5 = local_74 * DAT_006495cc;
      fVar10 = local_84 + local_74 * DAT_006495d4;
      fVar7 = local_88 + local_78 * 0.5;
      local_8c = fVar10 + fVar5 * DAT_006495d8;
      local_90 = fVar7 + fVar5 * DAT_006495dc;
      ImDrawList::AddCircleFilled(pIVar2,local_90,(uint)&local_90,(int)(fVar5 * DAT_006495d0));
      fVar7 = fVar7 + fVar5 * DAT_006495e4;
      local_8c = fVar10 + fVar5 * DAT_006495e0;
      local_90 = fVar7;
      ImDrawList::AddCircleFilled(pIVar2,DAT_006495e0,(uint)&local_90,(int)fVar5);
      local_8c = fVar10 + fVar5 * DAT_006495ec;
      local_90 = fVar7;
      ImDrawList::PathArcTo
                (pIVar2,(ImVec2 *)&local_90,DAT_006495ec,extraout_s1,extraout_s2,
                 (int)(fVar5 * DAT_006495e8));
      fVar5 = (float)ImDrawList::AddConvexPolyFilled
                               (pIVar2,*(ImVec2 **)(pIVar2 + 0x5c),*(int *)(pIVar2 + 0x54),
                                0xffdcdcdc);
      iVar4 = DAT_00649610;
      *(undefined4 *)(pIVar2 + 0x54) = 0;
      ImGui::CalcTextSize((char *)&local_90,(char *)(iVar4 + 0x649366),false,fVar5);
      local_94 = (local_84 + local_74 * fVar6) - local_8c * 0.5;
      local_98 = local_88 + (local_78 - local_90) * 0.5;
      ImDrawList::AddText(pIVar2,(uint)&local_98,(char *)0xffffffff,(char *)(iVar4 + 0x649366));
      if (iVar1 != 0) {
        FUN_00649878();
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(1);
  }
  if (**(int **)(DAT_00649614 + 0x64957e) != local_44) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


// ===== UI::FontsLoading @ 008024f4

/* UI::FontsLoading() */

bool UI::FontsLoading(void)

{
  return *(int *)(DAT_00802508 + 0x8024fe) != 0;
}


// ===== HUD::RenderLoading @ 00803d6c

/* HUD::RenderLoading(float) */

undefined4 HUD::RenderLoading(float param_1)

{
  bool in_r0;
  ImDrawList *pIVar1;
  ImDrawData *pIVar2;
  int iVar3;
  int iVar4;
  int *piVar5;
  uint in_fpscr;
  undefined4 uVar6;
  float extraout_s0;
  undefined4 uVar7;
  
  piVar5 = *(int **)(DAT_00803e1c + 0x803d7a);
  iVar3 = *piVar5;
  if (iVar3 != 0) {
    iVar4 = *(int *)(iVar3 + 4);
    if (0x3f < iVar4) {
      iVar4 = *(int *)(iVar3 + 8);
    }
    if (((0x3f < iVar4) &&
        (((*(char *)(DAT_00803e20 + 0x803d9a) != '\0' || (iVar3 = Init(), iVar3 != 0)) &&
         (iVar3 = ImGui::GetCurrentContext(), iVar3 != 0)))) && (*(char *)(iVar3 + 0x3de4) == '\0'))
    {
      iVar3 = ImGui::GetIO();
      iVar4 = *piVar5;
      uVar6 = VectorSignedToFloat(*(undefined4 *)(iVar4 + 4),(byte)(in_fpscr >> 0x16) & 3);
      uVar7 = VectorSignedToFloat(*(undefined4 *)(iVar4 + 8),(byte)(in_fpscr >> 0x16) & 3);
      *(undefined4 *)(iVar3 + 8) = uVar6;
      *(undefined4 *)(iVar3 + 0xc) = uVar7;
      ImGui_ImplRenderWare_NewFrame();
      if (*(float *)(iVar3 + 0x10) <= 0.0) {
        *(undefined4 *)(iVar3 + 0x10) = 0x3d088889;
      }
      ImGui::NewFrame();
      pIVar1 = (ImDrawList *)ImGui::GetBackgroundDrawList();
      MP::DrawMenuLoading(pIVar1,extraout_s0,in_r0);
      ImGui::EndFrame();
      ImGui::Render();
      pIVar2 = (ImDrawData *)ImGui::GetDrawData();
      ImGui_ImplRenderWare_RenderDrawData(pIVar2);
      return 1;
    }
  }
  return 0;
}


// ===== MP::MenuMpLoading @ 009d47e0

void MP::MenuMpLoading(void)

{
  (*(code *)PTR_MenuMpLoading_009f77b8)();
  return;
}


// ===== MP::PCMenuLoadingActive @ 009d47f0

void MP::PCMenuLoadingActive(void)

{
  (*(code *)PTR_PCMenuLoadingActive_009f77bc)();
  return;
}


// ===== HUD::RenderLoading @ 009d4800

void HUD::RenderLoading(float param_1)

{
  (*(code *)PTR_RenderLoading_009f77c0)();
  return;
}


// ===== MP::MenuMpBlack @ 009d4870

void MP::MenuMpBlack(void)

{
  (*(code *)PTR_MenuMpBlack_009f77dc)();
  return;
}


// ===== MP::DrawMenuLoading @ 009d68e0

void MP::DrawMenuLoading(ImDrawList *param_1,float param_2,bool param_3)

{
  (*(code *)PTR_DrawMenuLoading_009f7ff8)(param_1,param_3);
  return;
}


// ===== MP::DrawMenuMpBack @ 009d6910

void MP::DrawMenuMpBack(ImDrawList *param_1)

{
  (*(code *)PTR_DrawMenuMpBack_009f8004)(param_1);
  return;
}


// ===== MP::DrawMenuMpFront @ 009d6920

void MP::DrawMenuMpFront(void)

{
  (*(code *)PTR_DrawMenuMpFront_009f8008)();
  return;
}


// ===== UI::FontsLoading @ 009d6fb0

void UI::FontsLoading(void)

{
  (*(code *)PTR_FontsLoading_009f81ac)();
  return;
}


