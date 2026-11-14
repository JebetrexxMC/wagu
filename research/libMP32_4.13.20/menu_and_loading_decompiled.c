// Ghidra 12.2 decompile of research/libMP32.so (MP 4.13.20, armeabi-v7a), functions that use the main menu /
// loading screen strings. Ghidra loads the .so at 0x10000: file offset = address - 0x10000.

// ===== DrawMenuMpFront @ 00649080  strings: [##MPMenuTile, ##mpTile]

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


// ===== DrawMenuLoading @ 0064886c  strings: [CONNECTING TO MULTIPLAYER, GAMEPLAY TIP, LOADING WORLD]

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


// ===== InitMainMenu @ 00648670  strings: [MainMenuMultiplayer, PCMenu loading screen found: MP uses it for Multiplayer loads, PCMenuLoading]

/* MP::InitMainMenu() */

void MP::InitMainMenu(void)

{
  char cVar1;
  int iVar2;
  code *pcVar3;
  int *piVar4;
  undefined4 *puVar5;
  char *pcVar6;
  undefined4 *puVar7;
  int *piVar8;
  
  cVar1 = Config::GetBool((Config *)**(undefined4 **)(DAT_00648710 + 0x648682),
                          (char *)(DAT_00648714 + 0x648686),true,(char *)(DAT_00648718 + 0x64868a));
  pcVar6 = (char *)(DAT_0064871c + 0x648696);
  piVar8 = (int *)(DAT_00648720 + 0x648698);
  *pcVar6 = cVar1;
  DataMemoryBarrier(0x1b);
  puVar7 = (undefined4 *)(DAT_00648724 + 0x6486a4);
  if ((((*piVar8 == 0) && (iVar2 = dlopen(DAT_00648728 + 0x6486cc,0), iVar2 != 0)) &&
      (pcVar3 = (code *)dlsym(iVar2,DAT_0064872c + 0x6486d8), pcVar3 != (code *)0x0)) &&
     (((piVar4 = (int *)(*pcVar3)(DAT_00648730 + 0x6486e6), iVar2 = DAT_00648734,
       piVar4 != (int *)0x0 && (0 < *piVar4)) && (piVar4[1] != 0)))) {
    puVar5 = (undefined4 *)*puVar7;
    DataMemoryBarrier(0x1b);
    *piVar8 = (int)piVar4;
    DataMemoryBarrier(0x1b);
    Logger::Info((char *)*puVar5,iVar2 + 0x648708);
  }
  iVar2 = DAT_0064873c + 0x6486b4;
  if (*pcVar6 == '\0') {
    iVar2 = DAT_00648738 + 0x6486ae;
  }
  (*(code *)PTR_Info_009f7718)(*(undefined4 *)*puVar7,iVar2);
  return;
}


// ===== OnModLoad @ 00616df0  strings: [Loading screen: %d hooks, percent %s, _ZN10MobileMenu6UpdateEv, _ZN14CLoadingScreen12RenderSplashEv, _ZN14CLoadingScreen16RenderLoadingBarEv]

void OnModLoad(void)

{
  int iVar1;
  undefined4 uVar2;
  int *piVar3;
  code *pcVar4;
  int iVar5;
  int iVar6;
  int *piVar7;
  int iVar8;
  undefined4 *puVar9;
  uint uVar10;
  int *piVar11;
  int *piVar12;
  int *piVar13;
  undefined4 *puVar14;
  uint local_1c4 [2];
  void *local_1bc;
  char acStack_1b8 [400];
  int local_28;
  
  local_28 = **(int **)(DAT_006178b4 + 0x616e06);
  FUN_00612f08(DAT_00617078 + 0x616e04);
  std::set_terminate((_func_void *)(DAT_0061707c + 0x616e14));
  puVar14 = *(undefined4 **)(DAT_00617080 + 0x616e1c);
  iVar8 = DAT_00617084 + 0x616e2a;
  iVar1 = (**(code **)(*(int *)*puVar14 + 0x10))((int *)*puVar14,iVar8);
  piVar3 = (int *)*puVar14;
  piVar13 = *(int **)(DAT_00617088 + 0x616e38);
  pcVar4 = *(code **)(*piVar3 + 0x70);
  *piVar13 = iVar1;
  iVar1 = (*pcVar4)(piVar3,iVar8);
  piVar3 = *(int **)(DAT_0061708c + 0x616e52);
  iVar8 = *piVar13;
  *piVar3 = iVar1;
  puVar9 = (undefined4 *)(DAT_00617090 + 0x616e66);
  if (iVar1 == 0 || iVar8 == 0) {
    iVar1 = *(int *)*puVar9;
    if (**(int **)(DAT_006178b8 + 0x616e74) == local_28) {
      iVar8 = DAT_00617094 + 0x616e82;
LAB_0061706a:
      (*(code *)PTR_Error_009f7818)(iVar1,iVar8);
      return;
    }
  }
  else {
    uVar2 = (**(code **)(*(int *)*puVar14 + 0x14))((int *)*puVar14,iVar1,DAT_00617098 + 0x616e8e);
    piVar7 = (int *)*puVar14;
    iVar1 = *piVar3;
    iVar8 = DAT_006170a0 + 0x616ea6;
    pcVar4 = *(code **)(*piVar7 + 0x14);
    **(undefined4 **)(DAT_0061709c + 0x616e9a) = uVar2;
    uVar2 = (*pcVar4)(piVar7,iVar1,iVar8);
    piVar7 = (int *)*puVar14;
    iVar1 = *piVar3;
    iVar8 = DAT_006170a8 + 0x616ec2;
    pcVar4 = *(code **)(*piVar7 + 0x14);
    **(undefined4 **)(DAT_006170a4 + 0x616eb6) = uVar2;
    uVar2 = (*pcVar4)(piVar7,iVar1,iVar8);
    piVar7 = (int *)*puVar14;
    iVar1 = *piVar3;
    iVar8 = DAT_006170b0 + 0x616ede;
    pcVar4 = *(code **)(*piVar7 + 0x14);
    **(undefined4 **)(DAT_006170ac + 0x616ed2) = uVar2;
    iVar1 = (*pcVar4)(piVar7,iVar1,iVar8);
    piVar7 = (int *)*puVar14;
    piVar11 = (int *)(DAT_006170b4 + 0x616ef0);
    iVar8 = *piVar3;
    iVar5 = DAT_006170b8 + 0x616ef8;
    pcVar4 = *(code **)(*piVar7 + 0x14);
    *piVar11 = iVar1;
    iVar1 = (*pcVar4)(piVar7,iVar8,iVar5);
    piVar7 = (int *)(DAT_006170bc + 0x616f04);
    *piVar7 = iVar1;
    if (iVar1 == 0) {
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006170c0 + 0x616f16);
      *piVar7 = iVar1;
    }
    piVar7 = (int *)*puVar9;
    iVar8 = DAT_006170c8 + 0x616f2a;
    if (iVar1 == 0) {
      iVar8 = DAT_006170c4 + 0x616f26;
    }
    if (*piVar11 == 0) {
      iVar8 = DAT_006170c4 + 0x616f26;
    }
    Logger::Info((char *)*piVar7,DAT_006170cc + 0x616f34,iVar8);
    iVar1 = Game::Init((void *)*piVar3);
    if (iVar1 == 0) {
      iVar1 = *piVar7;
      if (**(int **)(DAT_006178bc + 0x61705e) == local_28) {
        iVar8 = DAT_00617108 + 0x61706c;
        goto LAB_0061706a;
      }
    }
    else {
      TYFMC::DataDir((TYFMC *)local_1c4,(char *)(DAT_006170d0 + 0x616f56),
                     (char **)(DAT_006170d4 + 0x616f5a),1);
      FUN_006179d0(acStack_1b8);
      if ((local_1c4[0] & 1) != 0) {
        operator_delete(local_1bc,local_1c4[0] & 0xfffffffe);
      }
      ScreenGuard::Init(acStack_1b8);
      MP::Init(acStack_1b8);
      ScriptGuard::Init((void *)*piVar3);
      ScriptGuard::InitFaultGuard((void *)*piVar3);
      piVar11 = (int *)*puVar14;
      uVar2 = (**(code **)(*piVar11 + 0x14))(piVar11,*piVar3,DAT_006170d8 + 0x616fb4);
      (**(code **)(*piVar11 + 0x18))
                (piVar11,uVar2,*(undefined4 *)(DAT_006170dc + 0x616fc2),
                 *(undefined4 *)(DAT_006170e0 + 0x616fc4));
      piVar11 = (int *)*puVar14;
      uVar2 = (**(code **)(*piVar11 + 0x14))(piVar11,*piVar3,DAT_006170e4 + 0x616fde);
      (**(code **)(*piVar11 + 0x18))
                (piVar11,uVar2,*(undefined4 *)(DAT_006170e8 + 0x616fec),
                 *(undefined4 *)(DAT_006170ec + 0x616fee));
      piVar11 = (int *)*puVar14;
      uVar2 = (**(code **)(*piVar11 + 0x14))(piVar11,*piVar3,DAT_006170f0 + 0x617008);
      (**(code **)(*piVar11 + 0x18))
                (piVar11,uVar2,*(undefined4 *)(DAT_006170f4 + 0x617016),
                 *(undefined4 *)(DAT_006170f8 + 0x617018));
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006170fc + 0x617030);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617100 + 0x617044),
                   *(undefined4 *)(DAT_00617104 + 0x617048));
      }
      uVar10 = (uint)(iVar1 != 0);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617124 + 0x61711e);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617144 + 0x617136),
                   *(undefined4 *)(DAT_00617148 + 0x61713a));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617164 + 0x61715c);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617184 + 0x617176),
                   *(undefined4 *)(DAT_00617188 + 0x61717a));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006171a4 + 0x61719c);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006171c4 + 0x6171b6),
                   *(undefined4 *)(DAT_006171c8 + 0x6171ba));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_0061757c + 0x6171dc);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617580 + 0x6171ee),
                   *(undefined4 *)(DAT_00617584 + 0x6171f2));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617588 + 0x61720a);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_0061758c + 0x61721c),
                   *(undefined4 *)(DAT_00617590 + 0x617220));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617594 + 0x617238);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617598 + 0x61724a),
                   *(undefined4 *)(DAT_0061759c + 0x61724e));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175a0 + 0x617266);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175a4 + 0x617278),
                   *(undefined4 *)(DAT_006175a8 + 0x61727c));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175ac + 0x617294);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175b0 + 0x6172a6),
                   *(undefined4 *)(DAT_006175b4 + 0x6172aa));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175b8 + 0x6172c2);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175bc + 0x6172d4),
                   *(undefined4 *)(DAT_006175c0 + 0x6172d8));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175c4 + 0x6172f0);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175c8 + 0x617302),
                   *(undefined4 *)(DAT_006175cc + 0x617306));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175d0 + 0x61731e);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175d4 + 0x617330),
                   *(undefined4 *)(DAT_006175d8 + 0x617334));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175dc + 0x61734c);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175e0 + 0x61735e),
                   *(undefined4 *)(DAT_006175e4 + 0x617362));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175e8 + 0x61737a);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175ec + 0x61738c),
                   *(undefined4 *)(DAT_006175f0 + 0x617390));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006175f4 + 0x6173a8);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006175f8 + 0x6173ba),
                   *(undefined4 *)(DAT_006175fc + 0x6173be));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617600 + 0x6173d6);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617604 + 0x6173e8),
                   *(undefined4 *)(DAT_00617608 + 0x6173ec));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_0061760c + 0x617404);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617610 + 0x617416),
                   *(undefined4 *)(DAT_00617614 + 0x61741a));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617618 + 0x617432);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_0061761c + 0x617444),
                   *(undefined4 *)(DAT_00617620 + 0x617448));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617624 + 0x617460);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617628 + 0x617472),
                   *(undefined4 *)(DAT_0061762c + 0x617476));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617630 + 0x61748e);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617634 + 0x6174a0),
                   *(undefined4 *)(DAT_00617638 + 0x6174a4));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_0061763c + 0x6174bc);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617640 + 0x6174ce),
                   *(undefined4 *)(DAT_00617644 + 0x6174d2));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617648 + 0x6174ea);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006178c0 + 0x6174fc),
                   *(undefined4 *)(DAT_006178c4 + 0x617500));
        uVar10 = uVar10 + 1;
      }
      Logger::Info((char *)*piVar7,DAT_006178c8 + 0x617512,uVar10);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006178cc + 0x617524);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006178d0 + 0x617536),
                   *(undefined4 *)(DAT_006178d4 + 0x61753a));
        Logger::Info((char *)*piVar7,DAT_006178d8 + 0x617548);
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006178dc + 0x61755a);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006178e0 + 0x61756e),
                   *(undefined4 *)(DAT_006178e4 + 0x617572));
      }
      uVar10 = (uint)(iVar1 != 0);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006178e8 + 0x61765e);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_006178ec + 0x617674),
                   *(undefined4 *)(DAT_006178f0 + 0x617678));
        uVar10 = uVar10 + 1;
      }
      Logger::Info((char *)*piVar7,DAT_006178f4 + 0x61768a,uVar10);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006178f8 + 0x61769e);
      iVar8 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_006178fc + 0x6176b2);
      if (iVar1 != 0) {
        puVar9 = *(undefined4 **)(DAT_00617904 + 0x6176cc);
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617900 + 0x6176ca),puVar9);
        *(undefined4 *)(DAT_00617908 + 0x6176de) = *puVar9;
      }
      iVar6 = DAT_0061790c + 0x6176ea;
      iVar5 = DAT_00617910 + 0x6176ec;
      if (iVar8 == 0) {
        iVar8 = iVar6;
      }
      else {
        puVar9 = *(undefined4 **)(DAT_00617918 + 0x6176fc);
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar8,*(undefined4 *)(DAT_00617914 + 0x6176fa),puVar9);
        iVar8 = DAT_00617920 + 0x61771e;
        *(undefined4 *)(DAT_0061791c + 0x61771a) = *puVar9;
      }
      if (iVar1 == 0) {
        iVar5 = iVar6;
      }
      Logger::Info((char *)*piVar7,DAT_00617924 + 0x617730,iVar5,iVar8);
      Game::InitMenu((void *)*piVar3);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617928 + 0x617754);
      piVar11 = (int *)*puVar14;
      piVar12 = (int *)(DAT_0061792c + 0x617762);
      iVar8 = *piVar3;
      iVar5 = DAT_00617930 + 0x61776a;
      pcVar4 = *(code **)(*piVar11 + 0x14);
      *piVar12 = iVar1;
      iVar1 = (*pcVar4)(piVar11,iVar8,iVar5);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617934 + 0x617780),
                   *(undefined4 *)(DAT_00617938 + 0x617784));
      }
      uVar10 = (uint)(iVar1 != 0);
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_0061793c + 0x6177a4);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_00617940 + 0x6177b6),
                   *(undefined4 *)(DAT_00617944 + 0x6177ba));
        uVar10 = uVar10 + 1;
      }
      iVar1 = (**(code **)(*(int *)*puVar14 + 0x14))
                        ((int *)*puVar14,*piVar3,DAT_00617948 + 0x6177d2);
      if (iVar1 != 0) {
        (**(code **)(*(int *)*puVar14 + 0x18))
                  ((int *)*puVar14,iVar1,*(undefined4 *)(DAT_0061794c + 0x6177e4),
                   *(undefined4 *)(DAT_00617950 + 0x6177e8));
        uVar10 = uVar10 + 1;
      }
      iVar1 = DAT_00617958 + 0x617804;
      if (*piVar12 != 0) {
        iVar1 = DAT_00617954 + 0x6177fa;
      }
      Logger::Info((char *)*piVar7,DAT_0061795c + 0x617806,uVar10,iVar1);
      piVar11 = (int *)*puVar14;
      if (*(int *)(&DAT_00202020 + *piVar13) == -0x17424fff) {
        iVar1 = *piVar13 + 0x675de4;
        iVar8 = 0x1c;
      }
      else {
        iVar1 = (**(code **)(*piVar11 + 0x14))(piVar11,*piVar3,DAT_00617960 + 0x617848);
        iVar8 = 0x18;
      }
      (**(code **)(*piVar11 + iVar8))
                (piVar11,iVar1,*(undefined4 *)(DAT_00617964 + 0x617858),
                 *(undefined4 *)(DAT_00617968 + 0x61785a));
      *(undefined1 *)(DAT_0061796c + 0x61786a) = 1;
      MP::SetMenuApiReady();
      Logger::Info((char *)*piVar7,DAT_00617970 + 0x61787c,DAT_00617974 + 0x61787e,acStack_1b8);
      iVar1 = local_28;
      if (**(int **)(DAT_00617978 + 0x617888) == local_28) {
        return;
      }
    }
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(iVar1);
}


// ===== AddMainMenuTile @ 0074e110  strings: [Main menu: SAUtils is older than 1.4.1 - MP draws its own Multiplayer tile, Main menu: [%s] tile added (SAUtils)]

/* Game::AddMainMenuTile(char const*, char const*, char const*, void (*)(void*)) */

void Game::AddMainMenuTile(char *param_1,char *param_2,char *param_3,_func_void_void_ptr *param_4)

{
  char cVar1;
  uint uVar2;
  FILE *__s;
  int iVar3;
  code *pcVar4;
  size_t sVar5;
  int extraout_r1;
  int *piVar6;
  char *pcVar7;
  undefined4 *puVar8;
  bool bVar9;
  stat sStack_90;
  int local_24;
  
  local_24 = **(int **)(DAT_0074e25c + 0x74e124);
  cVar1 = '\0';
  if (param_4 != (_func_void_void_ptr *)0x0) {
    puVar8 = *(undefined4 **)(DAT_0074e260 + 0x74e136);
    piVar6 = (int *)*puVar8;
    if (piVar6 != (int *)0x0) {
      if (param_3 != (char *)0x0) {
        cVar1 = *param_3;
      }
      if (param_3 != (char *)0x0 && cVar1 != '\0') {
        uVar2 = stat(param_3,&sStack_90);
        bVar9 = uVar2 == 0;
        iVar3 = extraout_r1;
        if (bVar9) {
          uVar2 = sStack_90.st_blksize ^ 0xe6a;
          iVar3 = sStack_90.st_blocks;
        }
        if ((!bVar9 || (uVar2 != 0 || iVar3 != 0)) &&
           (__s = fopen(param_3,(char *)(DAT_0074e264 + 0x74e172)), __s != (FILE *)0x0)) {
          fwrite((void *)(DAT_0074e268 + 0x74e184),1,0xe6a,__s);
          fclose(__s);
        }
        piVar6 = (int *)*puVar8;
      }
      iVar3 = (**(code **)(*piVar6 + 8))(piVar6,DAT_0074e26c + 0x74e19a);
      if (iVar3 != 0) {
        iVar3 = (**(code **)(*(int *)*puVar8 + 0xc))
                          ((int *)*puVar8,DAT_0074e270 + 0x74e1aa,DAT_0074e274 + 0x74e1ae);
        if (iVar3 == 0) {
          Logger::Info((char *)**(undefined4 **)(DAT_0074e278 + 0x74e206),DAT_0074e27c + 0x74e208);
          cVar1 = '\0';
          goto LAB_0074e242;
        }
        iVar3 = dlopen(DAT_0074e280 + 0x74e1ba,0);
        if (iVar3 == 0) {
          cVar1 = '\0';
          goto LAB_0074e242;
        }
        pcVar4 = (code *)dlsym(iVar3,DAT_0074e284 + 0x74e1c4);
        if ((pcVar4 != (code *)0x0) &&
           (piVar6 = (int *)(*pcVar4)(DAT_0074e288 + 0x74e1d0), piVar6 != (int *)0x0)) {
          if (param_2 == (char *)0x0) {
            pcVar7 = (char *)(DAT_0074e28c + 0x74e21c);
          }
          else {
            pcVar7 = (char *)(DAT_0074e290 + 0x74e1e4);
            if ((*param_2 != '\0') && (sVar5 = strlen(param_2), sVar5 < 0x20)) {
              pcVar7 = param_2;
            }
          }
          (**(code **)(*piVar6 + 0xa4))(piVar6,param_1,param_4,0,pcVar7,0);
          Logger::Info((char *)**(undefined4 **)(DAT_0074e294 + 0x74e238),DAT_0074e298 + 0x74e23a,
                       param_1);
          cVar1 = '\x01';
          goto LAB_0074e242;
        }
      }
      cVar1 = '\0';
    }
  }
LAB_0074e242:
  if (**(int **)(DAT_0074e29c + 0x74e24a) != local_24) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(cVar1);
  }
  return;
}


// ===== Process @ 0061f960  strings: [Main menu: Multiplayer -> black screen (server select in %u ms), Main menu: Multiplayer -> on the server, Main menu: Multiplayer -> single player]
// decompile failed: Exception while decompiling 0061f960: process: timeout

// ===== FUN_006bc8a0 @ 006bc8a0  strings: [MPMultiplayer: Multiplayer requested by another mod (PCMenu), MPMultiplayer: called before MP finished loading - ignored, PCMenu loading screen found: MP uses it for Multiplayer loads, PCMenuLoading]

void FUN_006bc8a0(void)

{
  char *pcVar1;
  int iVar2;
  code *pcVar3;
  int *piVar4;
  undefined4 *puVar5;
  int *piVar6;
  
  puVar5 = *(undefined4 **)(DAT_006bc938 + 0x6bc8ac);
  DataMemoryBarrier(0x1b);
  pcVar1 = (char *)*puVar5;
  if (*(char *)(DAT_006bc93c + 0x6bc8ae) == '\0') {
    (*(code *)PTR_Info_009f7718)(pcVar1,DAT_006bc95c + 0x6bc8e2);
    return;
  }
  Logger::Info(pcVar1,DAT_006bc940 + 0x6bc8be);
  piVar6 = (int *)(DAT_006bc948 + 0x6bc8cc);
  *(undefined1 *)(DAT_006bc944 + 0x6bc8ca) = 1;
  DataMemoryBarrier(0x1b);
  if ((((*piVar6 == 0) && (iVar2 = dlopen(DAT_006bc94c + 0x6bc8f0,0), iVar2 != 0)) &&
      (pcVar3 = (code *)dlsym(iVar2,DAT_006bc950 + 0x6bc8fc), pcVar3 != (code *)0x0)) &&
     (((piVar4 = (int *)(*pcVar3)(DAT_006bc954 + 0x6bc90a), iVar2 = DAT_006bc958,
       piVar4 != (int *)0x0 && (0 < *piVar4)) && (piVar4[1] != 0)))) {
    DataMemoryBarrier(0x1b);
    *piVar6 = (int)piVar4;
    DataMemoryBarrier(0x1b);
    Logger::Info((char *)*puVar5,iVar2 + 0x6bc92a);
    FUN_00649878();
    return;
  }
  FUN_00649878();
  return;
}


// ===== OnModPreLoad @ 00616d10  strings: [MPMultiplayer]

void OnModPreLoad(void)

{
  undefined4 uVar1;
  int iVar2;
  code *pcVar3;
  int iVar4;
  undefined4 *puVar5;
  
  puVar5 = *(undefined4 **)(DAT_00616db4 + 0x616d1c);
  Logger::SetTag((Logger *)*puVar5,(char *)(DAT_00616db8 + 0x616d1e));
  uVar1 = FloatMenu::GetInterface();
  iVar2 = dlopen(DAT_00616dbc + 0x616d32,0);
  if ((iVar2 != 0) && (pcVar3 = (code *)dlsym(iVar2,DAT_00616dc0 + 0x616d3c), pcVar3 != (code *)0x0)
     ) {
    (*pcVar3)(DAT_00616dc4 + 0x616d4a,uVar1);
  }
  uVar1 = MP::GetMultiplayerApi();
  iVar2 = dlopen(DAT_00616dc8 + 0x616d58,0);
  if ((iVar2 != 0) && (pcVar3 = (code *)dlsym(iVar2,DAT_00616dcc + 0x616d62), pcVar3 != (code *)0x0)
     ) {
    (*pcVar3)(DAT_00616dd0 + 0x616d70,uVar1);
  }
  uVar1 = *puVar5;
  iVar2 = dlopen(DAT_00616dd4 + 0x616d7a,0);
  if (iVar2 == 0) {
    iVar2 = DAT_00616dd8 + 0x616da6;
  }
  else {
    pcVar3 = (code *)dlsym(iVar2,DAT_00616ddc + 0x616d84);
    iVar2 = DAT_00616de0 + 0x616d8c;
    if (pcVar3 != (code *)0x0) {
      iVar4 = (*pcVar3)(DAT_00616de4 + 0x616d94);
      if (iVar4 != 0) {
        iVar2 = DAT_00616de8 + 0x616d9c;
      }
    }
  }
  (*(code *)PTR_Info_009f7718)(uVar1,DAT_00616dec + 0x616dae,iVar2);
  return;
}


// ===== FUN_00649878 @ 00649878  strings: [Main menu: Multiplayer -> %s %d (%s)]

void FUN_00649878(void)

{
  char cVar1;
  bool bVar2;
  int iVar3;
  undefined4 *puVar4;
  int iVar5;
  undefined1 *puVar6;
  int iVar7;
  char *pcVar8;
  int iVar9;
  
  iVar3 = DAT_006499ac;
  if ((*(char *)(DAT_006499a0 + 0x649888) != '\0') || (*(char *)(DAT_006499a4 + 0x649890) != '\0'))
  {
    pcVar8 = (char *)(DAT_006499a8 + 0x6498a2);
    cVar1 = *pcVar8;
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    *pcVar8 = '\x01';
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    *(undefined1 *)(iVar3 + 0x6498b4) = 0;
    DataMemoryBarrier(0x1b);
    MP_NowMs();
    if ((cVar1 == '\x01') && (iVar3 = Game::LocalPed(), 0 < iVar3)) {
      (*(code *)PTR_Info_009f7718)
                (**(undefined4 **)(DAT_006499dc + 0x6498d8),DAT_006499e0 + 0x6498da);
      return;
    }
    iVar3 = DAT_006499b8;
    DataMemoryBarrier(0x1b);
    puVar6 = (undefined1 *)(DAT_006499b4 + 0x6498fa);
    *(undefined1 *)(DAT_006499b0 + 0x6498f4) = 0;
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    *(undefined4 *)(iVar3 + 0x6498fe) = 0;
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    *puVar6 = 1;
    iVar3 = DAT_006499bc;
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    puVar4 = (undefined4 *)(DAT_006499bc + 0x649920);
    do {
      ExclusiveAccess(puVar4);
      bVar2 = (bool)hasExclusiveAccess(puVar4);
    } while (!bVar2);
    *puVar4 = 0;
    *(undefined4 *)(iVar3 + 0x649924) = 0;
    DataMemoryBarrier(0x1b);
    DataMemoryBarrier(0x1b);
    *(undefined1 *)(DAT_006499c0 + 0x64993a) = 1;
    DataMemoryBarrier(0x1b);
    iVar3 = Game::NewestSaveSlot();
    if (iVar3 < 0) {
      iVar9 = DAT_006499c8 + 0x64995e;
      iVar5 = Game::RequestNewGame();
    }
    else {
      iVar5 = Game::RequestLoadSlot(iVar3);
      iVar9 = DAT_006499c4 + 0x649958;
    }
    iVar7 = DAT_006499d4 + 0x649976;
    if (iVar5 == 0) {
      iVar7 = DAT_006499d0 + 0x649972;
    }
    Logger::Info((char *)**(undefined4 **)(DAT_006499cc + 0x64996c),DAT_006499d8 + 0x649978,iVar9,
                 iVar3 + 1,iVar7);
    if (iVar5 == 0) {
      DataMemoryBarrier(0x1b);
      *pcVar8 = '\0';
      DataMemoryBarrier(0x1b);
    }
  }
  return;
}


// ===== InitMenu @ 0074dc74  strings: [Main menu: menu %s, main-screen check %s, sound fader %s, _ZN10MobileMenu16IsMainMenuScreenEv]

/* Game::InitMenu(void*) */

void Game::InitMenu(void *param_1)

{
  int iVar1;
  undefined4 uVar2;
  int *piVar3;
  int iVar4;
  int *piVar5;
  int *piVar6;
  int iVar7;
  code *pcVar8;
  int *piVar9;
  int *piVar10;
  int iVar11;
  int *piVar12;
  undefined4 *puVar13;
  
  if (param_1 == (void *)0x0) {
    return;
  }
  puVar13 = *(undefined4 **)(DAT_0074dde4 + 0x74dc8c);
  iVar1 = (**(code **)(*(int *)*puVar13 + 0x14))((int *)*puVar13,param_1,DAT_0074dde8 + 0x74dc9c);
  iVar4 = DAT_0074ddf0;
  piVar3 = (int *)*puVar13;
  piVar9 = (int *)(DAT_0074ddec + 0x74dca8);
  iVar7 = *piVar3;
  *piVar9 = iVar1;
  iVar1 = (**(code **)(iVar7 + 0x14))(piVar3,param_1,iVar4 + 0x74dcb4);
  iVar4 = DAT_0074ddf8;
  piVar3 = (int *)*puVar13;
  pcVar8 = *(code **)(*piVar3 + 0x14);
  piVar10 = (int *)(DAT_0074ddf4 + 0x74dcc6);
  *piVar10 = iVar1;
  uVar2 = (*pcVar8)(piVar3,param_1,iVar4 + 0x74dcce);
  piVar3 = (int *)*puVar13;
  pcVar8 = *(code **)(*piVar3 + 0x14);
  iVar4 = DAT_0074de00 + 0x74dce4;
  *(undefined4 *)(DAT_0074ddfc + 0x74dce2) = uVar2;
  iVar4 = (*pcVar8)(piVar3,param_1,iVar4);
  piVar3 = (int *)*puVar13;
  pcVar8 = *(code **)(*piVar3 + 0x14);
  piVar5 = (int *)(DAT_0074de04 + 0x74dcf8);
  *piVar5 = iVar4;
  iVar4 = (*pcVar8)(piVar3,param_1,DAT_0074de08 + 0x74dd04);
  piVar3 = (int *)*puVar13;
  piVar6 = (int *)(DAT_0074de0c + 0x74dd0e);
  *piVar6 = iVar4;
  uVar2 = (**(code **)(*piVar3 + 0x14))(piVar3,param_1,DAT_0074de10 + 0x74dd1e);
  piVar3 = (int *)*puVar13;
  *(undefined4 *)(DAT_0074de14 + 0x74dd28) = uVar2;
  iVar4 = (**(code **)(*piVar3 + 0x14))(piVar3,param_1,DAT_0074de18 + 0x74dd36);
  piVar12 = (int *)(DAT_0074de1c + 0x74dd3c);
  *piVar12 = iVar4;
  iVar4 = (**(code **)(*(int *)*puVar13 + 0x14))((int *)*puVar13,param_1,DAT_0074de20 + 0x74dd4c);
  piVar3 = (int *)(DAT_0074de24 + 0x74dd52);
  *piVar3 = iVar4;
  iVar7 = (**(code **)(*(int *)*puVar13 + 0x14))((int *)*puVar13,param_1,DAT_0074de28 + 0x74dd62);
  *(int *)(DAT_0074de2c + 0x74dd6a) = iVar7;
  puVar13 = *(undefined4 **)(DAT_0074de30 + 0x74dd72);
  iVar1 = DAT_0074de34 + 0x74dd82;
  iVar11 = DAT_0074de38 + 0x74dd84;
  iVar4 = iVar1;
  if (iVar7 == 0) {
    iVar4 = iVar11;
  }
  iVar7 = iVar1;
  if (*piVar12 == 0 && *piVar3 == 0) {
    iVar7 = iVar11;
  }
  Logger::Info((char *)*puVar13,DAT_0074de3c + 0x74dd9a,iVar7,iVar4);
  iVar4 = iVar1;
  if (*piVar6 == 0) {
    iVar4 = iVar11;
  }
  if (*piVar5 == 0) {
    iVar4 = iVar11;
  }
  iVar7 = iVar1;
  if (*piVar9 == 0) {
    iVar7 = iVar11;
  }
  if (*piVar10 == 0) {
    iVar1 = iVar11;
  }
  Logger::Info((char *)*puVar13,DAT_0074de40 + 0x74ddc6,iVar7,iVar1,iVar4);
  return;
}


