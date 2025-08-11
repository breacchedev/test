#include "stdafx.h"
#include "utils.h"
#include "config.h"
#include "char.h"
#include "desc.h"
#include "sectree_manager.h"
#include "packet.h"
#include "protocol.h"
#include "log.h"
#include "skill.h"
#include "unique_item.h"
#include "profiler.h"
#include "marriage.h"
#include "item_addon.h"
#include "dev_log.h"
#include "locale_service.h"
#include "item.h"
#include "item_manager.h"
#include "affect.h"
#include "DragonSoul.h"
#include "buff_on_attributes.h"
#include "belt_inventory_helper.h"
#include "war_map.h"
#include "../../common/VnumHelper.h"
#include "../../common/CommonDefines.h"
#ifdef ENABLE_EXTENDED_ITEMNAME_ON_GROUND
#include "mob_manager.h"
#endif
#ifdef ENABLE_EXTENDED_PET_SYSTEM
#include "PetSystem.h"
#endif
CItem::CItem(DWORD dwVnum)
	: m_dwVnum(dwVnum), m_bWindow(0), m_dwID(0), m_bEquipped(false), m_dwVID(0), m_wCell(0), m_dwCount(0), m_lFlag(0), m_dwLastOwnerPID(0),
	m_bExchanging(false), m_pkDestroyEvent(NULL), m_pkExpireEvent(NULL), m_pkUniqueExpireEvent(NULL),
	m_pkTimerBasedOnWearExpireEvent(NULL), m_pkRealTimeExpireEvent(NULL),
   	m_pkAccessorySocketExpireEvent(NULL), m_pkOwnershipEvent(NULL), m_dwOwnershipPID(0), m_bSkipSave(false), m_isLocked(false),
	m_dwMaskVnum(0), m_dwSIGVnum (0)
#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	, m_dwEvolution(0)
#endif
#ifdef ENABLE_BASIC_ITEM_SYSTEM
	, is_basic(false)
#endif
{
	memset( &m_alSockets, 0, sizeof(m_alSockets) );
	memset( &m_aAttr, 0, sizeof(m_aAttr) );
}

CItem::~CItem()
{
	Destroy();
}

void CItem::Initialize()
{
	CEntity::Initialize(ENTITY_ITEM);

	m_bWindow = RESERVED_WINDOW;
	m_pOwner = NULL;
	m_dwID = 0;
	m_bEquipped = false;
	m_dwVID = m_wCell = m_dwCount = m_lFlag = 0;
	m_pProto = NULL;
	m_bExchanging = false;
	memset(&m_alSockets, 0, sizeof(m_alSockets));
	memset(&m_aAttr, 0, sizeof(m_aAttr));

	m_pkDestroyEvent = NULL;
	m_pkOwnershipEvent = NULL;
	m_dwOwnershipPID = 0;
	m_pkUniqueExpireEvent = NULL;
	m_pkTimerBasedOnWearExpireEvent = NULL;
	m_pkRealTimeExpireEvent = NULL;

	m_pkAccessorySocketExpireEvent = NULL;

	m_bSkipSave = false;
	m_dwLastOwnerPID = 0;
#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	m_dwEvolution = 0;
#endif
#ifdef ENABLE_BASIC_ITEM_SYSTEM
	is_basic = false;
#endif
}

void CItem::Destroy()
{
	event_cancel(&m_pkDestroyEvent);
	event_cancel(&m_pkOwnershipEvent);
	event_cancel(&m_pkUniqueExpireEvent);
	event_cancel(&m_pkTimerBasedOnWearExpireEvent);
	event_cancel(&m_pkRealTimeExpireEvent);
	event_cancel(&m_pkAccessorySocketExpireEvent);

	CEntity::Destroy();

	if (GetSectree())
		GetSectree()->RemoveEntity(this);
}

EVENTFUNC(item_destroy_event)
{
	item_event_info* info = dynamic_cast<item_event_info*>( event->info );

	if ( info == NULL )
	{
		sys_err( "item_destroy_event> <Factor> Null pointer" );
		return 0;
	}

	LPITEM pkItem = info->item;

	if (pkItem->GetOwner())
		sys_err("item_destroy_event: Owner exist. (item %s owner %s)", pkItem->GetName(), pkItem->GetOwner()->GetName());

	pkItem->SetDestroyEvent(NULL);
	M2_DESTROY_ITEM(pkItem);
	return 0;
}

void CItem::SetDestroyEvent(LPEVENT pkEvent)
{
	m_pkDestroyEvent = pkEvent;
}

void CItem::StartDestroyEvent(int iSec)
{
	if (m_pkDestroyEvent)
		return;

	item_event_info* info = AllocEventInfo<item_event_info>();
	info->item = this;

	SetDestroyEvent(event_create(item_destroy_event, info, PASSES_PER_SEC(iSec)));
}

void CItem::EncodeInsertPacket(LPENTITY ent)
{
	LPDESC d;

	if (!(d = ent->GetDesc()))
		return;

	const PIXEL_POSITION & c_pos = GetXYZ();

	struct packet_item_ground_add pack;

	pack.bHeader	= HEADER_GC_ITEM_GROUND_ADD;
	pack.x		= c_pos.x;
	pack.y		= c_pos.y;
	pack.z		= c_pos.z;
	pack.dwVnum		= GetVnum();
#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	pack.evolution = m_dwEvolution;
#endif
	pack.dwVID		= m_dwVID;
#ifdef ENABLE_EXTENDED_ITEMNAME_ON_GROUND
	for (size_t i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
		pack.alSockets[i] = GetSocket(i);

	thecore_memcpy(pack.aAttrs, GetAttributes(), sizeof(pack.aAttrs));
	pack.dwCount = m_dwCount;
#endif
	//pack.count= m_dwCount;

	d->Packet(&pack, sizeof(pack));

	if (m_pkOwnershipEvent != NULL)
	{
		item_event_info * info = dynamic_cast<item_event_info *>(m_pkOwnershipEvent->info);

		if ( info == NULL )
		{
			sys_err( "CItem::EncodeInsertPacket> <Factor> Null pointer" );
			return;
		}

		TPacketGCItemOwnership p;

		p.bHeader = HEADER_GC_ITEM_OWNERSHIP;
		p.dwVID = m_dwVID;
		strlcpy(p.szName, info->szOwnerName, sizeof(p.szName));

		d->Packet(&p, sizeof(TPacketGCItemOwnership));
	}
}

void CItem::EncodeRemovePacket(LPENTITY ent)
{
	LPDESC d;

	if (!(d = ent->GetDesc()))
		return;

	struct packet_item_ground_del pack;

	pack.bHeader	= HEADER_GC_ITEM_GROUND_DEL;
	pack.dwVID		= m_dwVID;

	d->Packet(&pack, sizeof(pack));
	sys_log(2, "Item::EncodeRemovePacket %s to %s", GetName(), ((LPCHARACTER) ent)->GetName());
}

void CItem::SetProto(const TItemTable * table)
{
	assert(table != NULL);
	m_pProto = table;
	SetFlag(m_pProto->dwFlags);
}

void CItem::UsePacketEncode(LPCHARACTER ch, LPCHARACTER victim, struct packet_item_use *packet)
{
	if (!GetVnum())
		return;

	packet->header 	= HEADER_GC_ITEM_USE;
	packet->ch_vid 	= ch->GetVID();
	packet->victim_vid 	= victim->GetVID();
	packet->Cell = TItemPos(GetWindow(), m_wCell);
	packet->vnum	= GetVnum();
}

void CItem::RemoveFlag(long bit)
{
	REMOVE_BIT(m_lFlag, bit);
}

void CItem::AddFlag(long bit)
{
	SET_BIT(m_lFlag, bit);
}

void CItem::UpdatePacket()
{
	if (!m_pOwner || !m_pOwner->GetDesc())
		return;

#ifdef ENABLE_SWITCHBOT
	if (m_bWindow == SWITCHBOT)
		return;
#endif

	TPacketGCItemUpdate pack;

	pack.header = HEADER_GC_ITEM_UPDATE;
	pack.Cell = TItemPos(GetWindow(), m_wCell);
	pack.count	= m_dwCount;

	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
		pack.alSockets[i] = m_alSockets[i];

	thecore_memcpy(pack.aAttr, GetAttributes(), sizeof(pack.aAttr));
#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	pack.evolution = m_dwEvolution;
#endif
#ifdef ENABLE_BASIC_ITEM_SYSTEM
	pack.is_basic = is_basic;
#endif
	sys_log(2, "UpdatePacket %s -> %s", GetName(), m_pOwner->GetName());
	m_pOwner->GetDesc()->Packet(&pack, sizeof(pack));
}

DWORD CItem::GetCount()
{
	if (GetType() == ITEM_ELK) return MIN(m_dwCount, INT_MAX);
	else
	{
#ifdef __EXTENDED_ITEM_COUNT__
		return MIN(m_dwCount, ITEM_MAX_COUNT);
#else
		return MIN(m_dwCount, g_bItemCountLimit);
#endif
	}
}

bool CItem::SetCount(DWORD count)
{
	if (GetType() == ITEM_ELK)
	{
		m_dwCount = MIN(count, INT_MAX);
	}
	else
	{
#ifdef __EXTENDED_ITEM_COUNT__
		m_dwCount = MIN(count, ITEM_MAX_COUNT);
#else
		m_dwCount = MIN(count, g_bItemCountLimit);
#endif
	}

	if (count == 0 && m_pOwner)
	{
		if (GetSubType() == USE_ABILITY_UP || GetSubType() == USE_POTION || GetVnum() == 70020)
		{
			LPCHARACTER pOwner = GetOwner();
			WORD wCell = GetCell();

			RemoveFromCharacter();

			if (!IsDragonSoul())
			{
				LPITEM pItem = pOwner->FindSpecifyItem(GetVnum());

				if (NULL != pItem)
				{
					pOwner->ChainQuickslotItem(pItem, QUICKSLOT_TYPE_ITEM, wCell);
				}
				else
				{
					pOwner->SyncQuickslot(QUICKSLOT_TYPE_ITEM, wCell, 255);
				}
			}

			M2_DESTROY_ITEM(this);
		}
		else
		{
			if (!IsDragonSoul())
			{
				m_pOwner->SyncQuickslot(QUICKSLOT_TYPE_ITEM, m_wCell, 255);
			}
			M2_DESTROY_ITEM(RemoveFromCharacter());
		}

		return false;
	}

	UpdatePacket();

	Save();
	return true;
}

LPITEM CItem::RemoveFromCharacter()
{
	if (!m_pOwner)
	{
		sys_err("Item::RemoveFromCharacter owner null");
		return (this);
	}

	LPCHARACTER pOwner = m_pOwner;

	if (m_bEquipped)
	{
		Unequip();

		SetWindow(RESERVED_WINDOW);
		Save();
		return (this);
	}
	else
	{
		if (GetWindow() != SAFEBOX && GetWindow() != MALL)
		{
			if (IsDragonSoul())
			{
				if (m_wCell >= DRAGON_SOUL_INVENTORY_MAX_NUM)
					sys_err("CItem::RemoveFromCharacter: pos >= DRAGON_SOUL_INVENTORY_MAX_NUM");
				else
					pOwner->SetItem(TItemPos(m_bWindow, m_wCell), NULL);
			}
#ifdef ENABLE_SWITCHBOT
			else if (m_bWindow == SWITCHBOT)
			{
				if (m_wCell >= SWITCHBOT_SLOT_COUNT)
				{
					sys_err("CItem::RemoveFromCharacter: pos >= SWITCHBOT_SLOT_COUNT");
				}
				else
				{
					pOwner->SetItem(TItemPos(SWITCHBOT, m_wCell), NULL);
				}
			}
#endif
			else
			{
				TItemPos cell(INVENTORY, m_wCell);

				if (false == cell.IsDefaultInventoryPosition() && false == cell.IsBeltInventoryPosition()
#ifdef ENABLE_SPECIAL_INVENTORY_SYSTEM
					&& false == cell.IsSkillBookInventoryPosition()
					&& false == cell.IsUpgradeItemsInventoryPosition()
					&& false == cell.IsStoneInventoryPosition()
					&& false == cell.IsGiftBoxInventoryPosition()
					&& false == cell.IsChangersInventoryPosition()
#endif
					)
					sys_err("CItem::RemoveFromCharacter: Invalid Item Position");
				else
				{
					pOwner->SetItem(cell, NULL);
				}
			}
		}

		m_pOwner = NULL;
		m_wCell = 0;

		SetWindow(RESERVED_WINDOW);
		Save();
		return (this);
	}
}

bool CItem::AddToCharacter(LPCHARACTER ch, TItemPos Cell)
{
	assert(GetSectree() == NULL);
	assert(m_pOwner == NULL);

	WORD pos = Cell.cell;
	BYTE window_type = Cell.window_type;

	if (INVENTORY == window_type)
	{
		if (m_wCell >= INVENTORY_MAX_NUM && BELT_INVENTORY_SLOT_START > m_wCell)
		{
			sys_err("CItem::AddToCharacter: cell overflow: %s to %s cell %d", m_pProto->szName, ch->GetName(), m_wCell);
			return false;
		}
	}
	else if (DRAGON_SOUL_INVENTORY == window_type)
	{
		if (m_wCell >= DRAGON_SOUL_INVENTORY_MAX_NUM)
		{
			sys_err("CItem::AddToCharacter: cell overflow: %s to %s cell %d", m_pProto->szName, ch->GetName(), m_wCell);
			return false;
		}
	}
#ifdef ENABLE_SWITCHBOT
	else if (SWITCHBOT == window_type)
	{
		if (m_wCell >= SWITCHBOT_SLOT_COUNT)
		{
			sys_err("CItem::AddToCharacter:switchbot cell overflow: %s to %s cell %d", m_pProto->szName, ch->GetName(), m_wCell);
			return false;
		}
	}
#endif

#ifdef ENABLE_HIGHLIGHT_NEW_ITEM
	bool bWereMine = (GetLastOwnerPID() == ch->GetPlayerID()) || (GetWindow() == EQUIPMENT);
#endif

	if (ch->GetDesc())
		m_dwLastOwnerPID = ch->GetPlayerID();

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
	if ((GetType() == ITEM_COSTUME) && (GetSubType() == COSTUME_ACCE) && (GetSocket(ACCE_ABSORPTION_SOCKET) == 0))
	{
		long lVal = GetValue(ACCE_GRADE_VALUE_FIELD);
		switch (lVal)
		{
		case 2:
			lVal = ACCE_GRADE_2_ABS;
			break;
		case 3:
			lVal = ACCE_GRADE_3_ABS;
			break;
		case 4:
			lVal = number(ACCE_GRADE_4_ABS_MIN, ACCE_GRADE_4_ABS_MAX_COMB);
			break;
		default:
			lVal = ACCE_GRADE_1_ABS;
			break;
		}
		SetSocket(ACCE_ABSORPTION_SOCKET, lVal);
	}
#endif

	event_cancel(&m_pkDestroyEvent);

#ifdef ENABLE_HIGHLIGHT_NEW_ITEM
	ch->SetItem(TItemPos(window_type, pos), this, bWereMine);
#else
	ch->SetItem(TItemPos(window_type, pos), this);
#endif
	m_pOwner = ch;

	Save();
	return true;
}

LPITEM CItem::RemoveFromGround()
{
	if (GetSectree())
	{
		SetOwnership(NULL);

		GetSectree()->RemoveEntity(this);

		ViewCleanup();

		Save();
	}

	return (this);
}

bool CItem::AddToGround(long lMapIndex, const PIXEL_POSITION & pos, bool skipOwnerCheck)
{
	if (0 == lMapIndex)
	{
		sys_err("wrong map index argument: %d", lMapIndex);
		return false;
	}

	if (GetSectree())
	{
		sys_err("sectree already assigned");
		return false;
	}

	if (!skipOwnerCheck && m_pOwner)
	{
		sys_err("owner pointer not null");
		return false;
	}

	LPSECTREE tree = SECTREE_MANAGER::instance().Get(lMapIndex, pos.x, pos.y);

	if (!tree)
	{
		sys_err("cannot find sectree by %dx%d", pos.x, pos.y);
		return false;
	}

	SetWindow(GROUND);
	SetXYZ(pos.x, pos.y, pos.z);
	tree->InsertEntity(this);
	UpdateSectree();
	Save();
	return true;
}

bool CItem::DistanceValid(LPCHARACTER ch)
{
	if (!GetSectree())
		return false;

	int iDist = DISTANCE_APPROX(GetX() - ch->GetX(), GetY() - ch->GetY());

	if (iDist > 600) // @fixme173 300 to 600
		return false;

	return true;
}

bool CItem::CanUsedBy(LPCHARACTER ch)
{
	// Anti flag check
	switch (ch->GetJob())
	{
		case JOB_WARRIOR:
			if (GetAntiFlag() & ITEM_ANTIFLAG_WARRIOR)
				return false;
			break;

		case JOB_ASSASSIN:
			if (GetAntiFlag() & ITEM_ANTIFLAG_ASSASSIN)
				return false;
			break;

		case JOB_SHAMAN:
			if (GetAntiFlag() & ITEM_ANTIFLAG_SHAMAN)
				return false;
			break;

		case JOB_SURA:
			if (GetAntiFlag() & ITEM_ANTIFLAG_SURA)
				return false;
			break;
#ifdef ENABLE_WOLFMAN_CHARACTER
		case JOB_WOLFMAN:
			if (GetAntiFlag() & ITEM_ANTIFLAG_WOLFMAN)
				return false;
			break;
#endif
	}

	return true;
}

int CItem::FindEquipCell(LPCHARACTER ch, int iCandidateCell)
{
	if ((0 == GetWearFlag() || ITEM_TOTEM == GetType()) && ITEM_COSTUME != GetType() && ITEM_DS != GetType() && ITEM_SPECIAL_DS != GetType() && ITEM_RING != GetType() && ITEM_BELT != GetType()
#ifdef ENABLE_EXTENDED_PET_SYSTEM
	&& !IsPetItem()
#endif
		)
		return -1;

	if (GetType() == ITEM_DS || GetType() == ITEM_SPECIAL_DS)
	{
		if (iCandidateCell < 0)
		{
			return WEAR_MAX_NUM + GetSubType();
		}
		else
		{
			for (int i = 0; i < DRAGON_SOUL_DECK_MAX_NUM; i++)
			{
				if (WEAR_MAX_NUM + i * DS_SLOT_MAX + GetSubType() == iCandidateCell)
				{
					return iCandidateCell;
				}
			}
			return -1;
		}
	}
	else if (GetType() == ITEM_COSTUME)
	{
		if (GetSubType() == COSTUME_BODY)
			return WEAR_COSTUME_BODY;
		else if (GetSubType() == COSTUME_HAIR)
			return WEAR_COSTUME_HAIR;
#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
#ifdef ENABLE_BLOCK_ITEMS_ON_WAR_MAP
		else if (GetSubType() == COSTUME_MOUNT && !CWarMapManager::instance().IsWarMap(m_pOwner->GetMapIndex()))
#else
		else if (GetSubType() == COSTUME_MOUNT)
#endif
			return WEAR_COSTUME_MOUNT;
#endif
#ifdef ENABLE_ACCE_COSTUME_SYSTEM
		else if (GetSubType() == COSTUME_ACCE)
			return WEAR_COSTUME_ACCE;
#endif
#ifdef ENABLE_WEAPON_COSTUME_SYSTEM
		else if (GetSubType() == COSTUME_WEAPON)
			return WEAR_COSTUME_WEAPON;
#endif
#ifdef RING_SYSTEM
		else if (GetSubType() == COSTUME_RING_HILAL)
			return WEAR_COSTUME_RING_HILAL;
		else if (GetSubType() == COSTUME_RING_LOLIPOP)
			return WEAR_COSTUME_RING_LOLIPOP;
		else if (GetSubType() == COSTUME_RING_MUTLULUK)
			return WEAR_COSTUME_RING_MUTLULUK;
		else if (GetSubType() == COSTUME_RING_OLUMCUL)
			return WEAR_COSTUME_RING_OLUMCUL;
		else if (GetSubType() == COSTUME_RING_KAHRAMANLIK)
			return WEAR_COSTUME_RING_KAHRAMANLIK;
		else if (GetSubType() == COSTUME_RING_CIKOLATA)
			return WEAR_COSTUME_RING_CIKOLATA;
		else if (GetSubType() == COSTUME_RING_NAZAR)
			return WEAR_COSTUME_RING_NAZAR;
		else if (GetSubType() == COSTUME_RING_BEKCILER)
			return WEAR_COSTUME_RING_BEKCILER;
#endif
	}

//#if !defined(ENABLE_MOUNT_COSTUME_SYSTEM) && !defined(ENABLE_ACCE_COSTUME_SYSTEM)
/* 	else if (GetType() == ITEM_RING)
	{
		if (ch->GetWear(WEAR_RING1))
			return WEAR_RING2;
		else
			return WEAR_RING1;
	} */
	else if (GetType() == ITEM_RING)//@BreaccheFix0075
	{
		switch(GetVnum())
		{
			case 71135:
			case 81117:
				return WEAR_RING1;
			case 71136:
			case 81118:
			case 81120:
				return WEAR_RING2;
			case 71143:
			case 71149:
			case 81121:
				return WEAR_RING3;
			case 71148:
			case 71158:
			case 81124:
				return WEAR_RING4;
		}
	}
//#endif
	else if (GetType() == ITEM_BELT)
		return WEAR_BELT;
	else if (GetWearFlag() & WEARABLE_BODY)
		return WEAR_BODY;
	else if (GetWearFlag() & WEARABLE_HEAD)
		return WEAR_HEAD;
	else if (GetWearFlag() & WEARABLE_FOOTS)
		return WEAR_FOOTS;
	else if (GetWearFlag() & WEARABLE_WRIST)
		return WEAR_WRIST;
	else if (GetWearFlag() & WEARABLE_WEAPON)
		return WEAR_WEAPON;
	else if (GetWearFlag() & WEARABLE_SHIELD)
		return WEAR_SHIELD;
	else if (GetWearFlag() & WEARABLE_NECK)
		return WEAR_NECK;
	else if (GetWearFlag() & WEARABLE_EAR)
		return WEAR_EAR;
	else if (GetWearFlag() & WEARABLE_ARROW)
		return WEAR_ARROW;

	else if (GetWearFlag() & WEARABLE_UNIQUE)
	{
		if (ch->GetWear(WEAR_UNIQUE1))
			return WEAR_UNIQUE2;
		else
			return WEAR_UNIQUE1;
	}


	else if (GetWearFlag() & WEARABLE_ABILITY)
	{
		if (!ch->GetWear(WEAR_ABILITY1))
		{
			return WEAR_ABILITY1;
		}
		else if (!ch->GetWear(WEAR_ABILITY2))
		{
			return WEAR_ABILITY2;
		}
		else if (!ch->GetWear(WEAR_ABILITY3))
		{
			return WEAR_ABILITY3;
		}
		else if (!ch->GetWear(WEAR_ABILITY4))
		{
			return WEAR_ABILITY4;
		}
		else if (!ch->GetWear(WEAR_ABILITY5))
		{
			return WEAR_ABILITY5;
		}
		else if (!ch->GetWear(WEAR_ABILITY6))
		{
			return WEAR_ABILITY6;
		}
		else if (!ch->GetWear(WEAR_ABILITY7))
		{
			return WEAR_ABILITY7;
		}
		else if (!ch->GetWear(WEAR_ABILITY8))
		{
			return WEAR_ABILITY8;
		}
		else
		{
			return -1;
		}
	}

#ifdef ENABLE_EXTENDED_PET_SYSTEM
	if (IsPetItem()
#ifdef ENABLE_BLOCK_ITEMS_ON_WAR_MAP
	&& !CWarMapManager::instance().IsWarMap(m_pOwner->GetMapIndex()))
#endif
		return WEAR_PET;
#endif
	return -1;
}

void CItem::ModifyPoints(bool bAdd)
{
	int accessoryGrade;


	if (false == IsAccessoryForSocket())
	{
#ifdef ENABLE_QUIVER_SYSTEM
		if ((m_pProto->bType == ITEM_WEAPON || m_pProto->bType == ITEM_ARMOR) && m_pProto->bSubType != WEAPON_QUIVER)
#else
		if (m_pProto->bType == ITEM_WEAPON || m_pProto->bType == ITEM_ARMOR)
#endif
		{
/* 			//@BreaccheFix0055 BEGIN
			for (int i = 0; i < ITEM_LIMIT_MAX_NUM; i++) {
				if (LIMIT_REAL_TIME == m_pProto->aLimits->bType) {
					continue;
				}
			}
			//@BreaccheFix0055 END */
			for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
			{
/* 				if (IsRealTimeItem() == true)//@BreaccheFix0055
					continue;//@BreaccheFix0055 */
				DWORD dwVnum;

#ifdef ENABLE_EXTRA_SOCKET_SYSTEM
				if ((dwVnum = GetSocket(i)) <= 3)
#else
				if ((dwVnum = GetSocket(i)) <= 2)
#endif
					continue;

				TItemTable * p = ITEM_MANAGER::instance().GetTable(dwVnum);

				if (!p)
				{
					sys_err("cannot find table by vnum %u", dwVnum);
					continue;
				}

				if (ITEM_METIN == p->bType)
				{
					//m_pOwner->ApplyPoint(p->alValues[0], bAdd ? p->alValues[1] : -p->alValues[1]);
					for (int i = 0; i < ITEM_APPLY_MAX_NUM; ++i)
					{
						if (p->aApplies[i].bType == APPLY_NONE)
							continue;
#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
						if (IsMountItem())
							continue;
#endif
						if (p->aApplies[i].bType == APPLY_SKILL)
							m_pOwner->ApplyPoint(p->aApplies[i].bType, bAdd ? p->aApplies[i].lValue : p->aApplies[i].lValue ^ 0x00800000);
						else
							m_pOwner->ApplyPoint(p->aApplies[i].bType, bAdd ? p->aApplies[i].lValue : -p->aApplies[i].lValue);
					}
				}
			}
		}

		accessoryGrade = 0;
	}
	else
	{
		accessoryGrade = MIN(GetAccessorySocketGrade(), ITEM_ACCESSORY_SOCKET_MAX_NUM);
	}

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
	if ((GetType() == ITEM_COSTUME) && (GetSubType() == COSTUME_ACCE) && (GetSocket(ACCE_ABSORBED_SOCKET)))
	{
		TItemTable * pkItemAbsorbed = ITEM_MANAGER::instance().GetTable(GetSocket(ACCE_ABSORBED_SOCKET));
		if (pkItemAbsorbed)
		{
			if ((pkItemAbsorbed->bType == ITEM_ARMOR) && (pkItemAbsorbed->bSubType == ARMOR_BODY))
			{
				// basic defense value from armor
				auto lDefGrade = CalcAcceBonus(pkItemAbsorbed->alValues[1] + (pkItemAbsorbed->alValues[5] * 2));
				m_pOwner->ApplyPoint(APPLY_DEF_GRADE_BONUS, bAdd ? lDefGrade : -lDefGrade);
				// basic magic defense value from armor
				auto lDefMagicBonus = CalcAcceBonus(pkItemAbsorbed->alValues[0]);
				m_pOwner->ApplyPoint(APPLY_MAGIC_DEF_GRADE, bAdd ? lDefMagicBonus : -lDefMagicBonus);
			}
			else if (pkItemAbsorbed->bType == ITEM_WEAPON)
			{
				// basic attack value from weapon
				if (pkItemAbsorbed->alValues[3] + pkItemAbsorbed->alValues[4] > 0)
				{
					auto lAttGrade = CalcAcceBonus(pkItemAbsorbed->alValues[4] + pkItemAbsorbed->alValues[5]);
					if (pkItemAbsorbed->alValues[3] > pkItemAbsorbed->alValues[4])
						lAttGrade = CalcAcceBonus(pkItemAbsorbed->alValues[3] + pkItemAbsorbed->alValues[5]);
					m_pOwner->ApplyPoint(APPLY_ATT_GRADE_BONUS, bAdd ? lAttGrade : -lAttGrade);
				}
				// basic magic attack value from weapon
				if (pkItemAbsorbed->alValues[1] + pkItemAbsorbed->alValues[2] > 0)
				{
					long lAttMagicGrade = CalcAcceBonus(pkItemAbsorbed->alValues[2] + pkItemAbsorbed->alValues[5]);
					if (pkItemAbsorbed->alValues[1] > pkItemAbsorbed->alValues[2])
						lAttMagicGrade = CalcAcceBonus(pkItemAbsorbed->alValues[1] + pkItemAbsorbed->alValues[5]);
					m_pOwner->ApplyPoint(APPLY_MAGIC_ATT_GRADE, bAdd ? lAttMagicGrade : -lAttMagicGrade);
				}
			}
		}
	}
#endif

	for (int i = 0; i < ITEM_APPLY_MAX_NUM; ++i)
	{
		if (m_pProto->aApplies[i].bType == APPLY_NONE)
			continue;
		int value = m_pProto->aApplies[i].lValue;
		if (m_pProto->aApplies[i].bType == APPLY_SKILL)
			m_pOwner->ApplyPoint(m_pProto->aApplies[i].bType, bAdd ? value : value ^ 0x00800000);
		else
		{
			if (0 != accessoryGrade && i < ITEM_APPLY_MAX_NUM - 1) // @fixme170
				value += MAX(accessoryGrade, value * aiAccessorySocketEffectivePct[accessoryGrade] / 100);
			m_pOwner->ApplyPoint(m_pProto->aApplies[i].bType, bAdd ? value : -value);
		}
	}

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
	if (GetType() == ITEM_COSTUME && GetSubType() == COSTUME_ACCE)
	{
		TItemTable * pkItemAbsorbed = ITEM_MANAGER::instance().GetTable(GetSocket(ACCE_ABSORBED_SOCKET));
		if (pkItemAbsorbed)
		{
			for (int i = 0; i < ITEM_APPLY_MAX_NUM; ++i)
			{
				if (pkItemAbsorbed->aApplies[i].bType == APPLY_NONE)
					continue;
				int value = CalcAcceBonus(pkItemAbsorbed->aApplies[i].lValue);
				if (pkItemAbsorbed->aApplies[i].bType == APPLY_SKILL)
					m_pOwner->ApplyPoint(pkItemAbsorbed->aApplies[i].bType, bAdd ? value : value ^ 0x00800000);
				else
				{
					if (0 != accessoryGrade && i < ITEM_APPLY_MAX_NUM - 1) // @fixme170
						value += MAX(accessoryGrade, value * aiAccessorySocketEffectivePct[accessoryGrade] / 100);
					m_pOwner->ApplyPoint(pkItemAbsorbed->aApplies[i].bType, bAdd ? value : -value);
				}
			}
		}
	}
#endif

	if (true == CItemVnumHelper::IsRamadanMoonRing(GetVnum()) || true == CItemVnumHelper::IsHalloweenCandy(GetVnum())
		|| true == CItemVnumHelper::IsHappinessRing(GetVnum()) || true == CItemVnumHelper::IsLovePendant(GetVnum()))
	{
		// Do not anything.
	}
	else
	{
		for (int i = 0; i < ITEM_ATTRIBUTE_MAX_NUM; ++i)
		{
			if (GetAttributeType(i))
			{
				const TPlayerItemAttribute &ia = GetAttribute(i);
				auto value = ia.sValue;
#ifdef ENABLE_ACCE_COSTUME_SYSTEM
				if (GetType() == ITEM_COSTUME && GetSubType() == COSTUME_ACCE)
					value = CalcAcceBonus(value);
#endif
				if (ia.bType == APPLY_SKILL)
					m_pOwner->ApplyPoint(ia.bType, bAdd ? value : value ^ 0x00800000);
				else
					m_pOwner->ApplyPoint(ia.bType, bAdd ? value : -value);
			}
		}
	}

	switch (m_pProto->bType)
	{
		case ITEM_PICK:
		case ITEM_ROD:
			{
				if (bAdd)
				{
					if (m_wCell == INVENTORY_MAX_NUM + WEAR_WEAPON)
						m_pOwner->SetPart(PART_WEAPON, GetVnum());
				}
				else
				{
					if (m_wCell == INVENTORY_MAX_NUM + WEAR_WEAPON)
						m_pOwner->SetPart(PART_WEAPON, 0);
				}
			}
			break;

		case ITEM_WEAPON:
			{
#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
		const DWORD itemEvoSTRPlus[] = {0,120,240,360,480,600};
		m_pOwner->ApplyPoint(APPLY_ATT_GRADE_BONUS,bAdd ? itemEvoSTRPlus[GetEvolution()] : -itemEvoSTRPlus[GetEvolution()]);
		if(GetValue(1) > 0 && GetValue(2) > 0)
			m_pOwner->ApplyPoint(APPLY_MAGIC_ATT_GRADE,bAdd ? itemEvoSTRPlus[GetEvolution()] : -itemEvoSTRPlus[GetEvolution()]);
#endif
#ifdef ENABLE_WEAPON_COSTUME_SYSTEM
				if (0 != m_pOwner->GetWear(WEAR_COSTUME_WEAPON))
					break;
#endif

				if (bAdd)
				{
					if (m_wCell == INVENTORY_MAX_NUM + WEAR_WEAPON)
						m_pOwner->SetPart(PART_WEAPON, GetVnum());
				}
				else
				{
					if (m_wCell == INVENTORY_MAX_NUM + WEAR_WEAPON)
						m_pOwner->SetPart(PART_WEAPON, 0);
				}
			}
			break;

		case ITEM_ARMOR:
			{

				if (0 != m_pOwner->GetWear(WEAR_COSTUME_BODY))
					break;

				if (GetSubType() == ARMOR_BODY || GetSubType() == ARMOR_HEAD || GetSubType() == ARMOR_FOOTS || GetSubType() == ARMOR_SHIELD)
				{
					if (bAdd)
					{
						if (GetProto()->bSubType == ARMOR_BODY)
							m_pOwner->SetPart(PART_MAIN, GetVnum());
					}
					else
					{
						if (GetProto()->bSubType == ARMOR_BODY)
							m_pOwner->SetPart(PART_MAIN, m_pOwner->GetOriginalPart(PART_MAIN));
					}
				}
			}
			break;


		case ITEM_COSTUME:
			{
				DWORD toSetValue = this->GetVnum();
				EParts toSetPart = PART_MAX_NUM;


				if (GetSubType() == COSTUME_BODY)
				{
					toSetPart = PART_MAIN;

					if (false == bAdd)
					{

						const CItem* pArmor = m_pOwner->GetWear(WEAR_BODY);
						toSetValue = (NULL != pArmor) ? pArmor->GetVnum() : m_pOwner->GetOriginalPart(PART_MAIN);
					}

				}

				else if (GetSubType() == COSTUME_HAIR)
				{
					toSetPart = PART_HAIR;
					toSetValue = (true == bAdd) ? this->GetValue(3) : 0;
				}

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
/* 				else if (GetSubType() == COSTUME_ACCE)
				{
					toSetValue = (bAdd == true) ? this->GetAcceVnum() : 0;
					toSetPart = PART_ACCE;
				} */
		else if (GetSubType() == COSTUME_ACCE)//start
		{
			toSetValue -= 85000;
			if (GetSocket(ACCE_ABSORPTION_SOCKET) >= ACCE_EFFECT_FROM_ABS)
				toSetValue += 500;

			toSetValue = (bAdd == true) ? toSetValue : 0;
			toSetPart = PART_ACCE;
		}//end
#endif

#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
				else if (GetSubType() == COSTUME_MOUNT)
				{
					// not need to do a thing in here
				}
#endif

#ifdef ENABLE_WEAPON_COSTUME_SYSTEM
				else if (GetSubType() == COSTUME_WEAPON)
				{
					toSetPart = PART_WEAPON;
					if (false == bAdd)
					{
						const CItem* pWeapon = m_pOwner->GetWear(WEAR_WEAPON);
						toSetValue = (NULL != pWeapon) ? pWeapon->GetVnum() : 0;
					}
				}
#endif

				if (PART_MAX_NUM != toSetPart)
				{
					m_pOwner->SetPart((BYTE)toSetPart, toSetValue);
					m_pOwner->UpdatePacket();
				}
			}
			break;
		case ITEM_UNIQUE:
			{
				if (0 != GetSIGVnum())
				{
					const CSpecialItemGroup* pItemGroup = ITEM_MANAGER::instance().GetSpecialItemGroup(GetSIGVnum());
					if (NULL == pItemGroup)
						break;
					DWORD dwAttrVnum = pItemGroup->GetAttrVnum(GetVnum());
					const CSpecialAttrGroup* pAttrGroup = ITEM_MANAGER::instance().GetSpecialAttrGroup(dwAttrVnum);
					if (NULL == pAttrGroup)
						break;
					for (itertype (pAttrGroup->m_vecAttrs) it = pAttrGroup->m_vecAttrs.begin(); it != pAttrGroup->m_vecAttrs.end(); it++)
					{
						m_pOwner->ApplyPoint(it->apply_type, bAdd ? it->apply_value : -it->apply_value);
					}
				}
			}
			break;
#ifdef ENABLE_SIG_FOR_ITEM_RING
	case ITEM_RING:
	{
		if (0 != GetSIGVnum())
		{
			const CSpecialItemGroup* pItemGroup = ITEM_MANAGER::instance().GetSpecialItemGroup(GetSIGVnum());
			if (NULL == pItemGroup)
				break;
			DWORD dwAttrVnum = pItemGroup->GetAttrVnum(GetVnum());
			const CSpecialAttrGroup* pAttrGroup = ITEM_MANAGER::instance().GetSpecialAttrGroup(dwAttrVnum);
			if (NULL == pAttrGroup)
				break;
			for (itertype(pAttrGroup->m_vecAttrs) it = pAttrGroup->m_vecAttrs.begin(); it != pAttrGroup->m_vecAttrs.end(); it++)
			{
				m_pOwner->ApplyPoint(it->apply_type, bAdd ? it->apply_value : -it->apply_value);
			}
		}
	}
	break;
#endif
	}
}

bool CItem::IsEquipable() const
{
	switch (this->GetType())
	{
	case ITEM_COSTUME:
	case ITEM_ARMOR:
	case ITEM_WEAPON:
	case ITEM_ROD:
	case ITEM_PICK:
	case ITEM_UNIQUE:
	case ITEM_DS:
	case ITEM_SPECIAL_DS:
	case ITEM_RING:
	case ITEM_BELT:
		return true;
	}

	return false;
}

#define ENABLE_IMMUNE_FIX
// return false on error state
bool CItem::EquipTo(LPCHARACTER ch, BYTE bWearCell)
{
	if (!ch)
	{
		sys_err("EquipTo: nil character");
		return false;
	}


	if (IsDragonSoul())
	{
		if (bWearCell < WEAR_MAX_NUM || bWearCell >= WEAR_MAX_NUM + DRAGON_SOUL_DECK_MAX_NUM * DS_SLOT_MAX)
		{
			sys_err("EquipTo: invalid dragon soul cell (this: #%d %s wearflag: %d cell: %d)", GetOriginalVnum(), GetName(), GetSubType(), bWearCell - WEAR_MAX_NUM);
			return false;
		}
	}
	else
	{
		if (bWearCell >= WEAR_MAX_NUM)
		{
			sys_err("EquipTo: invalid wear cell (this: #%d %s wearflag: %d cell: %d)", GetOriginalVnum(), GetName(), GetWearFlag(), bWearCell);
			return false;
		}
	}

	if (ch->GetWear(bWearCell))
	{
		sys_err("EquipTo: item already exist (this: #%d %s cell: %d %s)", GetOriginalVnum(), GetName(), bWearCell, ch->GetWear(bWearCell)->GetName());
		return false;
	}

	if (GetOwner())
		RemoveFromCharacter();

	ch->SetWear(bWearCell, this);

	m_pOwner = ch;
	m_bEquipped = true;
	m_wCell	= INVENTORY_MAX_NUM + bWearCell;

#ifndef ENABLE_IMMUNE_FIX
	DWORD dwImmuneFlag = 0;

	for (int i = 0; i < WEAR_MAX_NUM; ++i)
	{
		if (m_pOwner->GetWear(i))
		{
			// m_pOwner->ChatPacket(CHAT_TYPE_INFO, "unequip immuneflag(%u)", m_pOwner->GetWear(i)->m_pProto->dwImmuneFlag); // always 0
			SET_BIT(dwImmuneFlag, m_pOwner->GetWear(i)->m_pProto->dwImmuneFlag);
		}
	}

	m_pOwner->SetImmuneFlag(dwImmuneFlag);
#endif

	if (IsDragonSoul())
	{
		DSManager::instance().ActivateDragonSoul(this);
	}
	else
	{
		ModifyPoints(true);
		StartUniqueExpireEvent();
		if (-1 != GetProto()->cLimitTimerBasedOnWearIndex)
			StartTimerBasedOnWearExpireEvent();

		// ACCESSORY_REFINE
		StartAccessorySocketExpireEvent();
		// END_OF_ACCESSORY_REFINE
	}
#ifdef ENABLE_EXTENDED_PET_SYSTEM
	if (IsPetItem())
	{
		CPetSystem* petSystem = ch->GetPetSystem();
		if (petSystem)
			petSystem->Summon(GetValue(0), this, 0, false);
	}
#endif
	ch->BuffOnAttr_AddBuffsFromItem(this);
	m_pOwner->ComputeBattlePoints();

#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
	if (IsMountItem())
		ch->MountSummon(this);
#endif

	m_pOwner->UpdatePacket();

#ifdef ENABLE_HIGHLIGHT_NEW_ITEM
	SetLastOwnerPID(m_pOwner->GetPlayerID());
#endif
	Save();

	return (true);
}

bool CItem::Unequip()
{
	if (!m_pOwner || GetCell() < INVENTORY_MAX_NUM)
	{
		// ITEM_OWNER_INVALID_PTR_BUG
		sys_err("%s %u m_pOwner %p, GetCell %d",
				GetName(), GetID(), get_pointer(m_pOwner), GetCell());
		// END_OF_ITEM_OWNER_INVALID_PTR_BUG
		return false;
	}

	if (this != m_pOwner->GetWear(GetCell() - INVENTORY_MAX_NUM))
	{
		sys_err("m_pOwner->GetWear() != this");
		return false;
	}
#ifdef ENABLE_EXTENDED_PET_SYSTEM
	if (IsPetItem())
	{
		CPetSystem* petSystem = m_pOwner->GetPetSystem();
		if (petSystem)
			petSystem->Unsummon(GetValue(0));
	}
#endif
#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
	if (IsMountItem())
		m_pOwner->MountUnsummon(this);
#endif

	if (IsRideItem())
		ClearMountAttributeAndAffect();

	if (IsDragonSoul())
	{
		DSManager::instance().DeactivateDragonSoul(this);
	}
	else
	{
		ModifyPoints(false);
	}

	StopUniqueExpireEvent();

	if (-1 != GetProto()->cLimitTimerBasedOnWearIndex)
		StopTimerBasedOnWearExpireEvent();

	// ACCESSORY_REFINE
	StopAccessorySocketExpireEvent();
	// END_OF_ACCESSORY_REFINE

	m_pOwner->BuffOnAttr_RemoveBuffsFromItem(this);

	m_pOwner->SetWear(GetCell() - INVENTORY_MAX_NUM, NULL);

#ifndef ENABLE_IMMUNE_FIX
	DWORD dwImmuneFlag = 0;

	for (int i = 0; i < WEAR_MAX_NUM; ++i)
	{
		if (m_pOwner->GetWear(i))
		{
			// m_pOwner->ChatPacket(CHAT_TYPE_INFO, "unequip immuneflag(%u)", m_pOwner->GetWear(i)->m_pProto->dwImmuneFlag); // always 0
			SET_BIT(dwImmuneFlag, m_pOwner->GetWear(i)->m_pProto->dwImmuneFlag);
		}
	}

	m_pOwner->SetImmuneFlag(dwImmuneFlag);
#endif

	m_pOwner->ComputeBattlePoints();

	m_pOwner->UpdatePacket();

	m_pOwner = NULL;
	m_wCell = 0;
	m_bEquipped	= false;

	return true;
}

long CItem::GetValue(DWORD idx)
{
	assert(idx < ITEM_VALUES_MAX_NUM);
	return GetProto()->alValues[idx];
}

void CItem::SetExchanging(bool bOn)
{
	m_bExchanging = bOn;
}

void CItem::Save()
{
	if (m_bSkipSave)
		return;

	ITEM_MANAGER::instance().DelayedSave(this);
}

bool CItem::CreateSocket(BYTE bSlot, BYTE bGold)
{
	assert(bSlot < ITEM_SOCKET_MAX_NUM);

	if (m_alSockets[bSlot] != 0)
	{
		sys_err("Item::CreateSocket : socket already exist %s %d", GetName(), bSlot);
		return false;
	}

	if (bGold)
		m_alSockets[bSlot] = 2;
	else
		m_alSockets[bSlot] = 1;

	UpdatePacket();

	Save();
	return true;
}

void CItem::SetSockets(const long * c_al)
{
	thecore_memcpy(m_alSockets, c_al, sizeof(m_alSockets));
	Save();
}

void CItem::SetSocket(int i, long v, bool bLog)
{
	assert(i < ITEM_SOCKET_MAX_NUM);
	m_alSockets[i] = v;
	UpdatePacket();
	Save();
	if (bLog)
	{
#ifdef ENABLE_NEWSTUFF
		if (g_iDbLogLevel>=LOG_LEVEL_MAX)
#endif
		LogManager::instance().ItemLog(i, v, 0, GetID(), "SET_SOCKET", "", "", GetOriginalVnum());
	}
}

int CItem::GetGold()
{
	if (IS_SET(GetFlag(), ITEM_FLAG_COUNT_PER_1GOLD))
	{
		if (GetProto()->dwGold == 0)
			return GetCount();
		else
			return GetCount() / GetProto()->dwGold;
	}
	else
		return GetProto()->dwGold;
}

int CItem::GetShopBuyPrice()
{
	return GetProto()->dwShopBuyPrice;
}

bool CItem::IsOwnership(LPCHARACTER ch)
{
	if (!m_pkOwnershipEvent)
		return true;

	return m_dwOwnershipPID == ch->GetPlayerID() ? true : false;
}

EVENTFUNC(ownership_event)
{
	item_event_info* info = dynamic_cast<item_event_info*>( event->info );

	if (!info)
	{
		sys_err( "ownership_event> <Factor> Null pointer" );
		return 0;
	}

	LPITEM pkItem = info->item;

	pkItem->SetOwnershipEvent(NULL);

	TPacketGCItemOwnership p;

	p.bHeader	= HEADER_GC_ITEM_OWNERSHIP;
	p.dwVID	= pkItem->GetVID();
	p.szName[0]	= '\0';

	pkItem->PacketAround(&p, sizeof(p));
	return 0;
}

void CItem::SetOwnershipEvent(LPEVENT pkEvent)
{
	m_pkOwnershipEvent = pkEvent;
}

void CItem::SetOwnership(LPCHARACTER ch, int iSec)
{
	if (!ch)
	{
		if (m_pkOwnershipEvent)
		{
			event_cancel(&m_pkOwnershipEvent);
			m_dwOwnershipPID = 0;

			TPacketGCItemOwnership p;

			p.bHeader	= HEADER_GC_ITEM_OWNERSHIP;
			p.dwVID	= m_dwVID;
			p.szName[0]	= '\0';

			PacketAround(&p, sizeof(p));
		}
		return;
	}

	if (m_pkOwnershipEvent)
		return;

	if (iSec <= 10)
		iSec = 30;

	m_dwOwnershipPID = ch->GetPlayerID();

	item_event_info* info = AllocEventInfo<item_event_info>();
	strlcpy(info->szOwnerName, ch->GetName(), sizeof(info->szOwnerName));
	info->item = this;

	SetOwnershipEvent(event_create(ownership_event, info, PASSES_PER_SEC(iSec)));

	TPacketGCItemOwnership p;

	p.bHeader = HEADER_GC_ITEM_OWNERSHIP;
	p.dwVID = m_dwVID;
	strlcpy(p.szName, ch->GetName(), sizeof(p.szName));

	PacketAround(&p, sizeof(p));
}

int CItem::GetSocketCount()
{
	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; i++)
	{
		if (GetSocket(i) == 0)
			return i;
	}
	return ITEM_SOCKET_MAX_NUM;
}

bool CItem::AddSocket()
{
	int count = GetSocketCount();
	if (count == ITEM_SOCKET_MAX_NUM)
		return false;
	m_alSockets[count] = 1;
	return true;
}

void CItem::AlterToSocketItem(int iSocketCount)
{
	if (iSocketCount >= ITEM_SOCKET_MAX_NUM)
	{
		sys_log(0, "Invalid Socket Count %d, set to maximum", ITEM_SOCKET_MAX_NUM);
		iSocketCount = ITEM_SOCKET_MAX_NUM;
	}

	for (int i = 0; i < iSocketCount; ++i)
		SetSocket(i, 1);
}

void CItem::AlterToMagicItem()
{
	int idx = GetAttributeSetIndex();

	if (idx < 0)
		return;

	//	  Appearance Second Third
	// Weapon 50		20	 5
	// Armor  30		10	 2
	// Acc	20		10	 1

	int iSecondPct;
	int iThirdPct;

	switch (GetType())
	{
		case ITEM_WEAPON:
			iSecondPct = 20;
			iThirdPct = 5;
			break;

		case ITEM_ARMOR:
		case ITEM_COSTUME:
			if (GetSubType() == ARMOR_BODY)
			{
				iSecondPct = 10;
				iThirdPct = 2;
			}
			else
			{
				iSecondPct = 10;
				iThirdPct = 1;
			}
			break;

		default:
			return;
	}


	PutAttribute(aiItemMagicAttributePercentHigh);

	if (number(1, 100) <= iSecondPct)
		PutAttribute(aiItemMagicAttributePercentLow);

	if (number(1, 100) <= iThirdPct)
		PutAttribute(aiItemMagicAttributePercentLow);
}

DWORD CItem::GetRefineFromVnum()
{
	return ITEM_MANAGER::instance().GetRefineFromVnum(GetVnum());
}

int CItem::GetRefineLevel()
{
	const char* name = GetBaseName();
	char* p = const_cast<char*>(strrchr(name, '+'));

	if (!p)
		return 0;

	int	rtn = 0;
	str_to_number(rtn, p+1);

	const char* locale_name = GetName();
	p = const_cast<char*>(strrchr(locale_name, '+'));

	if (p)
	{
		int	locale_rtn = 0;
		str_to_number(locale_rtn, p+1);
		if (locale_rtn != rtn)
		{
			sys_err("refine_level_based_on_NAME(%d) is not equal to refine_level_based_on_LOCALE_NAME(%d).", rtn, locale_rtn);
		}
	}

	return rtn;
}

bool CItem::IsPolymorphItem()
{
	return GetType() == ITEM_POLYMORPH;
}

EVENTFUNC(unique_expire_event)
{
	item_event_info* info = dynamic_cast<item_event_info*>( event->info );

	if (!info)
	{
		sys_err( "unique_expire_event> <Factor> Null pointer" );
		return 0;
	}

	LPITEM pkItem = info->item;

	if (pkItem->GetValue(2) == 0)
	{
		if (pkItem->GetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME) <= 1)
		{
			sys_log(0, "UNIQUE_ITEM: expire %s %u", pkItem->GetName(), pkItem->GetID());
			pkItem->SetUniqueExpireEvent(NULL);
			ITEM_MANAGER::instance().RemoveItem(pkItem, "UNIQUE_EXPIRE");
			return 0;
		}
		else
		{
			pkItem->SetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME, pkItem->GetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME) - 1);
			return PASSES_PER_SEC(60);
		}
	}
	else
	{
		time_t cur = get_global_time();

		if (pkItem->GetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME) <= cur)
		{
			pkItem->SetUniqueExpireEvent(NULL);
			ITEM_MANAGER::instance().RemoveItem(pkItem, "UNIQUE_EXPIRE");
			return 0;
		}
		else
		{


			// by rtsummit
			if (pkItem->GetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME) - cur < 600)
				return PASSES_PER_SEC(pkItem->GetSocket(ITEM_SOCKET_UNIQUE_REMAIN_TIME) - cur);
			else
				return PASSES_PER_SEC(600);
		}
	}
}




EVENTFUNC(timer_based_on_wear_expire_event)
{
	item_event_info* info = dynamic_cast<item_event_info*>( event->info );

	if (!info)
	{
		sys_err( "expire_event <Factor> Null pointer" );
		return 0;
	}

	LPITEM pkItem = info->item;
	int remain_time = pkItem->GetSocket(ITEM_SOCKET_REMAIN_SEC) - processing_time/passes_per_sec;
	if (remain_time <= 0)
	{
		sys_log(0, "ITEM EXPIRED : expired %s %u", pkItem->GetName(), pkItem->GetID());
		pkItem->SetTimerBasedOnWearExpireEvent(NULL);
		pkItem->SetSocket(ITEM_SOCKET_REMAIN_SEC, 0);


		if (pkItem->IsDragonSoul())
		{
			DSManager::instance().DeactivateDragonSoul(pkItem);
		}
		else
		{
			ITEM_MANAGER::instance().RemoveItem(pkItem, "TIMER_BASED_ON_WEAR_EXPIRE");
		}
		return 0;
	}
	pkItem->SetSocket(ITEM_SOCKET_REMAIN_SEC, remain_time);
	return PASSES_PER_SEC (MIN (60, remain_time));
}

void CItem::SetUniqueExpireEvent(LPEVENT pkEvent)
{
	m_pkUniqueExpireEvent = pkEvent;
}

void CItem::SetTimerBasedOnWearExpireEvent(LPEVENT pkEvent)
{
	m_pkTimerBasedOnWearExpireEvent = pkEvent;
}

EVENTFUNC(real_time_expire_event)
{
	const item_vid_event_info* info = reinterpret_cast<const item_vid_event_info*>(event->info);

	if (!info)
		return 0;

	const LPITEM item = ITEM_MANAGER::instance().FindByVID( info->item_vid );

	if (!item)
		return 0;

	const time_t current = get_global_time();

	if (current > item->GetSocket(0))
	{
		if (item->GetVnum() && item->IsNewMountItem()) // @fixme152
			item->ClearMountAttributeAndAffect();

		ITEM_MANAGER::instance().RemoveItem(item, "REAL_TIME_EXPIRE");

		return 0;
	}

	return PASSES_PER_SEC(1);
}

void CItem::StartRealTimeExpireEvent()
{
	if (m_pkRealTimeExpireEvent)
		return;
	for (int i=0 ; i < ITEM_LIMIT_MAX_NUM ; i++)
	{
		if (LIMIT_REAL_TIME == GetProto()->aLimits[i].bType || LIMIT_REAL_TIME_START_FIRST_USE == GetProto()->aLimits[i].bType)
		{
			item_vid_event_info* info = AllocEventInfo<item_vid_event_info>();
			info->item_vid = GetVID();

			m_pkRealTimeExpireEvent = event_create( real_time_expire_event, info, PASSES_PER_SEC(1));

			sys_log(0, "REAL_TIME_EXPIRE: StartRealTimeExpireEvent");

			return;
		}
	}
}

bool CItem::IsRealTimeItem()
{
	if(!GetProto())
		return false;
	for (int i=0 ; i < ITEM_LIMIT_MAX_NUM ; i++)
	{
		if (LIMIT_REAL_TIME == GetProto()->aLimits[i].bType)
			return true;
	}
	return false;
}

void CItem::StartUniqueExpireEvent()
{
	if (GetType() != ITEM_UNIQUE)
		return;

	if (m_pkUniqueExpireEvent)
		return;


	if (IsRealTimeItem())
		return;

	// HARD CODING
	if (GetVnum() == UNIQUE_ITEM_HIDE_ALIGNMENT_TITLE)
		m_pOwner->ShowAlignment(false);

	int iSec = GetSocket(ITEM_SOCKET_UNIQUE_SAVE_TIME);

	if (iSec == 0)
		iSec = 60;
	else
		iSec = MIN(iSec, 60);

	SetSocket(ITEM_SOCKET_UNIQUE_SAVE_TIME, 0);

	item_event_info* info = AllocEventInfo<item_event_info>();
	info->item = this;

	SetUniqueExpireEvent(event_create(unique_expire_event, info, PASSES_PER_SEC(iSec)));
}


void CItem::StartTimerBasedOnWearExpireEvent()
{
	if (m_pkTimerBasedOnWearExpireEvent)
		return;


	if (IsRealTimeItem())
		return;

	if (-1 == GetProto()->cLimitTimerBasedOnWearIndex)
		return;

	int iSec = GetSocket(0);


	if (0 != iSec)
	{
		iSec %= 60;
		if (0 == iSec)
			iSec = 60;
	}

	item_event_info* info = AllocEventInfo<item_event_info>();
	info->item = this;

	SetTimerBasedOnWearExpireEvent(event_create(timer_based_on_wear_expire_event, info, PASSES_PER_SEC(iSec)));
}

void CItem::StopUniqueExpireEvent()
{
	if (!m_pkUniqueExpireEvent)
		return;

	if (GetValue(2) != 0)
		return;

	// HARD CODING
	if (GetVnum() == UNIQUE_ITEM_HIDE_ALIGNMENT_TITLE)
		m_pOwner->ShowAlignment(true);

	SetSocket(ITEM_SOCKET_UNIQUE_SAVE_TIME, event_time(m_pkUniqueExpireEvent) / passes_per_sec);
	event_cancel(&m_pkUniqueExpireEvent);

	ITEM_MANAGER::instance().SaveSingleItem(this);
}

void CItem::StopTimerBasedOnWearExpireEvent()
{
	if (!m_pkTimerBasedOnWearExpireEvent)
		return;

	int remain_time = GetSocket(ITEM_SOCKET_REMAIN_SEC) - event_processing_time(m_pkTimerBasedOnWearExpireEvent) / passes_per_sec;

	SetSocket(ITEM_SOCKET_REMAIN_SEC, remain_time);
	event_cancel(&m_pkTimerBasedOnWearExpireEvent);

	ITEM_MANAGER::instance().SaveSingleItem(this);
}

void CItem::ApplyAddon(int iAddonType)
{
	CItemAddonManager::instance().ApplyAddonTo(iAddonType, this);
}

int CItem::GetSpecialGroup() const
{
	return ITEM_MANAGER::instance().GetSpecialGroupFromItem(GetVnum());
}

bool CItem::IsAccessoryForSocket()
{
	return (m_pProto->bType == ITEM_ARMOR && (m_pProto->bSubType == ARMOR_WRIST || m_pProto->bSubType == ARMOR_NECK || m_pProto->bSubType == ARMOR_EAR)) ||
		(m_pProto->bType == ITEM_BELT);
}

#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
void CItem::SetAccessorySocketGrade(int iGrade, bool is_perma)
{
	SetSocket(0, MINMAX(0, iGrade, GetAccessorySocketMaxGrade()));

	int iDownTime = aiAccessorySocketDegradeTime[GetAccessorySocketGrade()];

	if (is_perma)
		SetAccessorySocketDownGradeTime(0);
	else
		SetAccessorySocketDownGradeTime(iDownTime);
}
#else
void CItem::SetAccessorySocketGrade(int iGrade)
{
	SetSocket(0, MINMAX(0, iGrade, GetAccessorySocketMaxGrade()));

	int iDownTime = aiAccessorySocketDegradeTime[GetAccessorySocketGrade()];

	SetAccessorySocketDownGradeTime(iDownTime);
}
#endif

void CItem::SetAccessorySocketMaxGrade(int iMaxGrade)
{
	SetSocket(1, MINMAX(0, iMaxGrade, ITEM_ACCESSORY_SOCKET_MAX_NUM));
}

void CItem::SetAccessorySocketDownGradeTime(DWORD time)
{
#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
	if (GetSocket(2) != 99)
#endif
	SetSocket(2, time);

	if (test_server && GetOwner())
		GetOwner()->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%s¿¡¼­ ¼ÒÄÏ ºüÁú¶§±îÁö ³²Àº ½Ã°£ %d"), GetName(), time);
}

EVENTFUNC(accessory_socket_expire_event)
{
	item_vid_event_info* info = dynamic_cast<item_vid_event_info*>( event->info );

	if (!info)
	{
		sys_err( "accessory_socket_expire_event> <Factor> Null pointer" );
		return 0;
	}

	LPITEM item = ITEM_MANAGER::instance().FindByVID(info->item_vid);

	if (item->GetAccessorySocketDownGradeTime() <= 1)
	{
degrade:
		item->SetAccessorySocketExpireEvent(NULL);
		item->AccessorySocketDegrade();
		return 0;
	}
	else
	{
		int iTime = item->GetAccessorySocketDownGradeTime() - 60;

		if (iTime <= 1)
			goto degrade;

		item->SetAccessorySocketDownGradeTime(iTime);

		if (iTime > 60)
			return PASSES_PER_SEC(60);
		else
			return PASSES_PER_SEC(iTime);
	}
}

void CItem::StartAccessorySocketExpireEvent()
{
	if (!IsAccessoryForSocket())
		return;

	if (m_pkAccessorySocketExpireEvent)
		return;

	if (GetAccessorySocketMaxGrade() == 0)
		return;

	if (GetAccessorySocketGrade() == 0)
		return;

	int iSec = GetAccessorySocketDownGradeTime();
	SetAccessorySocketExpireEvent(NULL);

	if (iSec <= 1)
		iSec = 5;
	else
		iSec = MIN(iSec, 60);

	item_vid_event_info* info = AllocEventInfo<item_vid_event_info>();
	info->item_vid = GetVID();

	SetAccessorySocketExpireEvent(event_create(accessory_socket_expire_event, info, PASSES_PER_SEC(iSec)));
}

void CItem::StopAccessorySocketExpireEvent()
{
	if (!m_pkAccessorySocketExpireEvent)
		return;

	if (!IsAccessoryForSocket())
		return;

	int new_time = GetAccessorySocketDownGradeTime() - (60 - event_time(m_pkAccessorySocketExpireEvent) / passes_per_sec);

	event_cancel(&m_pkAccessorySocketExpireEvent);

	if (new_time <= 1)
	{
		AccessorySocketDegrade();
	}
	else
	{
		SetAccessorySocketDownGradeTime(new_time);
	}
}

bool CItem::IsRideItem()
{
	if (ITEM_UNIQUE == GetType() && UNIQUE_SPECIAL_RIDE == GetSubType())
		return true;
	if (ITEM_UNIQUE == GetType() && UNIQUE_SPECIAL_MOUNT_RIDE == GetSubType())
		return true;
#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
	if (ITEM_COSTUME == GetType() && COSTUME_MOUNT == GetSubType())
		return true;
#endif
	return false;
}

bool CItem::IsRamadanRing()
{
	if (GetVnum() == UNIQUE_ITEM_RAMADAN_RING)
		return true;
	return false;
}

#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
bool CItem::IsMountItem()
{
	if (GetType() == ITEM_COSTUME && GetSubType() == COSTUME_MOUNT)
		return true;
	return false;
}
#endif

void CItem::ClearMountAttributeAndAffect()
{
	LPCHARACTER ch = GetOwner();
	if (!ch) // @fixme186
		return;

	ch->RemoveAffect(AFFECT_MOUNT);
	ch->RemoveAffect(AFFECT_MOUNT_BONUS);

	ch->MountVnum(0);

	ch->PointChange(POINT_ST, 0);
	ch->PointChange(POINT_DX, 0);
	ch->PointChange(POINT_HT, 0);
	ch->PointChange(POINT_IQ, 0);
}

bool CItem::IsNewMountItem()
{
	return (
		(ITEM_UNIQUE == GetType() && UNIQUE_SPECIAL_RIDE == GetSubType() && IS_SET(GetFlag(), ITEM_FLAG_QUEST_USE))
		|| (ITEM_UNIQUE == GetType() && UNIQUE_SPECIAL_MOUNT_RIDE == GetSubType() && IS_SET(GetFlag(), ITEM_FLAG_QUEST_USE))
#ifdef ENABLE_MOUNT_COSTUME_SYSTEM
		|| (ITEM_COSTUME == GetType() && COSTUME_MOUNT == GetSubType())
#endif
		); // @fixme152
}

void CItem::SetAccessorySocketExpireEvent(LPEVENT pkEvent)
{
	m_pkAccessorySocketExpireEvent = pkEvent;
}

void CItem::AccessorySocketDegrade()
{
	if (GetAccessorySocketGrade() > 0)
	{
#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
		if (GetSocket(2) == 99)
			return;
#endif
		LPCHARACTER ch = GetOwner();

		if (ch)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("%s¿¡ ¹ÚÇôÀÖ´ø º¸¼®ÀÌ »ç¶óÁý´Ï´Ù."), GetName());
		}

		ModifyPoints(false);
		SetAccessorySocketGrade(GetAccessorySocketGrade()-1);
		ModifyPoints(true);

		int iDownTime = aiAccessorySocketDegradeTime[GetAccessorySocketGrade()];

		if (test_server)
			iDownTime /= 60;

#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
		if (GetSocket(2) != 99)
#endif

		SetAccessorySocketDownGradeTime(iDownTime);

		if (iDownTime)
			StartAccessorySocketExpireEvent();
	}
}


static const bool CanPutIntoRing(LPITEM ring, LPITEM item)
{
	//const DWORD vnum = item->GetVnum();
	return false;
}

bool CItem::CanPutInto(LPITEM item)
{
	if (item->GetType() == ITEM_BELT)
		return this->GetSubType() == USE_PUT_INTO_BELT_SOCKET;

	else if(item->GetType() == ITEM_RING)
		return CanPutIntoRing(item, this);

	else if (item->GetType() != ITEM_ARMOR)
		return false;

	DWORD vnum = item->GetVnum();

	struct JewelAccessoryInfo
	{
		DWORD jewel;
		DWORD wrist;
		DWORD neck;
		DWORD ear;
	};
	const static JewelAccessoryInfo infos[] = {
#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
		{ 50641, 14220, 16220, 17220 }, 
//		{ 50642, 24500, 19500, 18500 },
		{ 50642, 14500, 16500, 17500 }, 
		{ 50643, 14520, 16520, 17520 }, 
//		{ 50643, 24520, 19520, 18520 },
		{ 50644, 14540, 16540, 17540 }, 
//		{ 50644, 24540, 19540, 18540 },
		{ 50645, 14560, 16560, 17560 }, 
//		{ 50645, 24560, 19560, 18560 },
//		{ 50646, 24570, 19570, 18570 },
		{ 50646, 14570, 16570, 17570 }, 
//		{ 50647, 14230, 16230, 17230 }, 
#else
		{ 50634, 14420, 16220, 17220 },
		{ 50635, 55310, 55330, 55340 },
		{ 50636, 55220, 55240, 55250 },
		{ 50637, 14540, 16540, 17540 },
		{ 50638, 14560, 16560, 17560 },
		{ 50639, 14570, 16570, 17570 },
#endif
	};

/* 		{ 50634, 14420, 16220, 17220 },
		{ 50635, 14500, 16500, 17500 },
		{ 50636, 14520, 16520, 17520 },
		{ 50637, 14540, 16540, 17540 },
		{ 50638, 14560, 16560, 17560 },
		{ 50639, 14570, 16570, 17570 }, */

	DWORD item_type = (item->GetVnum() / 10) * 10;
	for (size_t i = 0; i < sizeof(infos) / sizeof(infos[0]); i++)
	{
		const JewelAccessoryInfo& info = infos[i];
		switch(item->GetSubType())
		{
		case ARMOR_WRIST:
			if (info.wrist == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		case ARMOR_NECK:
			if (info.neck == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		case ARMOR_EAR:
			if (info.ear == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		}
	}
	if (item->GetSubType() == ARMOR_WRIST)
		vnum -= 14000;
	else if (item->GetSubType() == ARMOR_NECK)
		vnum -= 16000;
	else if (item->GetSubType() == ARMOR_EAR)
		vnum -= 17000;
	else
		return false;

	DWORD type = vnum / 20;

	if (type < 0 || type > 11)
	{
		type = (vnum - 170) / 20;

		if (50623 + type != GetVnum())
			return false;
		else
			return true;
	}
	else if (item->GetVnum() >= 16210 && item->GetVnum() <= 16219)
	{
		if (50625 != GetVnum())
			return false;
		else
			return true;
	}
	else if (item->GetVnum() >= 16230 && item->GetVnum() <= 16239)
	{
		if (50626 != GetVnum())
			return false;
		else
			return true;
	}

	return 50623 + type == GetVnum();
}

// PC_BANG_ITEM_ADD
bool CItem::IsPCBangItem()
{
	for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
	{
		if (m_pProto->aLimits[i].bType == LIMIT_PCBANG)
			return true;
	}
	return false;
}
// END_PC_BANG_ITEM_ADD

#ifdef ENABLE_PERMA_ACCESSORY_SYSTEM
bool CItem::CanPutIntoNew(LPITEM item)
{
	if (item->GetType() == ITEM_BELT)
		return this->GetSubType() == USE_PUT_INTO_BELT_SOCKET;

	else if (item->GetType() == ITEM_RING)
		return CanPutIntoRing(item, this);

	else if (item->GetType() != ITEM_ARMOR)
		return false;

	DWORD vnum = item->GetVnum();

	struct JewelAccessoryInfo
	{
		DWORD jewel;
		DWORD wrist;
		DWORD neck;
		DWORD ear;
	};
	const static JewelAccessoryInfo infos[] = {
		{ 50634, 14220, 16220, 17220 },
		{ 50635, 14500, 16500, 17500 },
		{ 50637, 14540, 16540, 17540 },
		{ 50638, 14560, 16560, 17560 },
		{ 50639, 14570, 16570, 17570 },
		{ 50640, 14230, 16230, 17230 },
	};

	DWORD item_type = (item->GetVnum() / 10) * 10;
	for (unsigned int i = 0; i < sizeof(infos) / sizeof(infos[0]); i++)
	{
		const JewelAccessoryInfo& info = infos[i];
		switch (item->GetSubType())
		{
		case ARMOR_WRIST:
			if (info.wrist == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		case ARMOR_NECK:
			if (info.neck == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		case ARMOR_EAR:
			if (info.ear == item_type)
			{
				if (info.jewel == GetVnum())
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			break;
		}
	}
	if (item->GetSubType() == ARMOR_WRIST)
		vnum -= 14000;
	else if (item->GetSubType() == ARMOR_NECK)
		vnum -= 16000;
	else if (item->GetSubType() == ARMOR_EAR)
		vnum -= 17000;
	else
		return false;

	DWORD type = vnum / 20;

	if (type < 0 || type > 11)
	{
		type = (vnum - 170) / 20;

		if (50623 + type != GetVnum())
			return false;
		else
			return true;
	}
	else if (item->GetVnum() >= 16210 && item->GetVnum() <= 16219)
	{
		if (50625 != GetVnum())
			return false;
		else
			return true;
	}
	else if (item->GetVnum() >= 16230 && item->GetVnum() <= 16239)
	{
		if (50626 != GetVnum())
			return false;
		else
			return true;
	}

	return 50623 + type == GetVnum();
}
#endif

bool CItem::CheckItemUseLevel(int nLevel)
{
	for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
	{
		if (this->m_pProto->aLimits[i].bType == LIMIT_LEVEL)
		{
			if (this->m_pProto->aLimits[i].lValue > nLevel) return false;
			else return true;
		}
	}
	return true;
}

long CItem::FindApplyValue(BYTE bApplyType)
{
	if (m_pProto == NULL)
		return 0;

	for (int i = 0; i < ITEM_APPLY_MAX_NUM; ++i)
	{
		if (m_pProto->aApplies[i].bType == bApplyType)
			return m_pProto->aApplies[i].lValue;
	}

	return 0;
}

void CItem::CopySocketTo(LPITEM pItem)
{
	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
	{
		pItem->m_alSockets[i] = m_alSockets[i];
	}
}

int CItem::GetAccessorySocketGrade()
{
   	return MINMAX(0, GetSocket(0), GetAccessorySocketMaxGrade());
}

int CItem::GetAccessorySocketMaxGrade()
{
   	return MINMAX(0, GetSocket(1), ITEM_ACCESSORY_SOCKET_MAX_NUM);
}

int CItem::GetAccessorySocketDownGradeTime()
{
	return MINMAX(0, GetSocket(2), aiAccessorySocketDegradeTime[GetAccessorySocketGrade()]);
}

void CItem::AttrLog()
{
	const char * pszIP = NULL;

	if (GetOwner() && GetOwner()->GetDesc())
		pszIP = GetOwner()->GetDesc()->GetHostName();

	for (int i = 0; i < ITEM_SOCKET_MAX_NUM; ++i)
	{
		if (m_alSockets[i])
		{
#ifdef ENABLE_NEWSTUFF
			if (g_iDbLogLevel>=LOG_LEVEL_MAX)
#endif
			LogManager::instance().ItemLog(i, m_alSockets[i], 0, GetID(), "INFO_SOCKET", "", pszIP ? pszIP : "", GetOriginalVnum());
		}
	}

	for (int i = 0; i<ITEM_ATTRIBUTE_MAX_NUM; ++i)
	{
		int	type	= m_aAttr[i].bType;
		int value	= m_aAttr[i].sValue;

		if (type)
		{
#ifdef ENABLE_NEWSTUFF
			if (g_iDbLogLevel>=LOG_LEVEL_MAX)
#endif
			LogManager::instance().ItemLog(i, type, value, GetID(), "INFO_ATTR", "", pszIP ? pszIP : "", GetOriginalVnum());
		}
	}
}

int CItem::GetLevelLimit()
{
	for (int i = 0; i < ITEM_LIMIT_MAX_NUM; ++i)
	{
		if (this->m_pProto->aLimits[i].bType == LIMIT_LEVEL)
		{
			return this->m_pProto->aLimits[i].lValue;
		}
	}
	return 0;
}

bool CItem::OnAfterCreatedItem()
{

	if (-1 != this->GetProto()->cLimitRealTimeFirstUseIndex)
	{

		if (0 != GetSocket(1))
		{
			StartRealTimeExpireEvent();
		}
	}

	return true;
}

bool CItem::IsDragonSoul()
{
	return GetType() == ITEM_DS;
}

int CItem::GiveMoreTime_Per(float fPercent)
{
	if (IsDragonSoul())
	{
		DWORD duration = DSManager::instance().GetDuration(this);
		DWORD remain_sec = GetSocket(ITEM_SOCKET_REMAIN_SEC);
		DWORD given_time = fPercent * duration / 100u;
		if (remain_sec == duration)
			return false;
		if ((given_time + remain_sec) >= duration)
		{
			SetSocket(ITEM_SOCKET_REMAIN_SEC, duration);
			return duration - remain_sec;
		}
		else
		{
			SetSocket(ITEM_SOCKET_REMAIN_SEC, given_time + remain_sec);
			return given_time;
		}
	}

	else
		return 0;
}

int CItem::GiveMoreTime_Fix(DWORD dwTime)
{
	if (IsDragonSoul())
	{
		DWORD duration = DSManager::instance().GetDuration(this);
		DWORD remain_sec = GetSocket(ITEM_SOCKET_REMAIN_SEC);
		if (remain_sec == duration)
			return false;
		if ((dwTime + remain_sec) >= duration)
		{
			SetSocket(ITEM_SOCKET_REMAIN_SEC, duration);
			return duration - remain_sec;
		}
		else
		{
			SetSocket(ITEM_SOCKET_REMAIN_SEC, dwTime + remain_sec);
			return dwTime;
		}
	}

	else
		return 0;
}


int	CItem::GetDuration()
{
	if(!GetProto())
		return -1;

	for (int i=0 ; i < ITEM_LIMIT_MAX_NUM ; i++)
	{
		if (LIMIT_REAL_TIME == GetProto()->aLimits[i].bType)
			return GetProto()->aLimits[i].lValue;
	}

	if (GetProto()->cLimitTimerBasedOnWearIndex >= 0)
	{
		BYTE cLTBOWI = GetProto()->cLimitTimerBasedOnWearIndex;
		return GetProto()->aLimits[cLTBOWI].lValue;
	}

	return -1;
}

bool CItem::IsSameSpecialGroup(const LPITEM item) const
{

	if (this->GetVnum() == item->GetVnum())
		return true;

	if (GetSpecialGroup() && (item->GetSpecialGroup() == GetSpecialGroup()))
		return true;

	return false;
}

#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
DWORD CItem::GetEvolution()
{
	return m_dwEvolution;
}
void CItem::SetEvolution(DWORD evolution)
{
	m_dwEvolution = evolution;
	UpdatePacket();
	Save();
}
#endif

#ifdef ENABLE_EXTENDED_ITEMNAME_ON_GROUND
const char* CItem::GetName()
{
	static char szItemName[128];
	memset(szItemName, 0, sizeof(szItemName));
	if (GetProto())
	{
		int len = 0;
		switch (GetType())
		{
		case ITEM_POLYMORPH:
		{
			const DWORD dwMobVnum = GetSocket(0);
			const CMob* pMob = CMobManager::instance().Get(dwMobVnum);
			if (pMob)
				len = snprintf(szItemName, sizeof(szItemName), "%s", pMob->m_table.szLocaleName);

			break;
		}
		case ITEM_SKILLBOOK:
		case ITEM_SKILLFORGET:
		{
			const DWORD dwSkillVnum = (GetVnum() == ITEM_SKILLBOOK_VNUM || GetVnum() == ITEM_SKILLFORGET_VNUM) ? GetSocket(0) : 0;
			const CSkillProto* pSkill = (dwSkillVnum != 0) ? CSkillManager::instance().Get(dwSkillVnum) : NULL;
			if (pSkill)
				len = snprintf(szItemName, sizeof(szItemName), "%s", pSkill->szName);

			break;
		}
		}
		len += snprintf(szItemName + len, sizeof(szItemName) - len, (len > 0) ? " %s" : "%s", GetProto()->szLocaleName);
	}

#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	if (m_pProto)
	{
		if (m_dwEvolution >= 1 && m_dwEvolution <= 5)
		{
			std::string evolutionMergeText = "";
			char evolutionText[5][10] = { "Yaygýn ", "Seyrek ", "Nadir ", "Efsane ", "Eþsiz " };
			evolutionMergeText += evolutionText[(m_dwEvolution - 1)];
			evolutionMergeText += m_pProto->szLocaleName;
			return  evolutionMergeText.c_str();
		}
	}
#endif

	return szItemName;
}

std::string CItem::GetNameString()
{
	static char szItemName[128];
	memset(szItemName, 0, sizeof(szItemName));
	if (GetProto())
	{
		int len = 0;
		switch (GetType())
		{
		case ITEM_POLYMORPH:
		{
			const DWORD dwMobVnum = GetSocket(0);
			const CMob* pMob = CMobManager::instance().Get(dwMobVnum);
			if (pMob)
				len = snprintf(szItemName, sizeof(szItemName), "%s", pMob->m_table.szLocaleName);

			break;
		}
		case ITEM_SKILLBOOK:
		case ITEM_SKILLFORGET:
		{
			const DWORD dwSkillVnum = (GetVnum() == ITEM_SKILLBOOK_VNUM || GetVnum() == ITEM_SKILLFORGET_VNUM) ? GetSocket(0) : 0;
			const CSkillProto* pSkill = (dwSkillVnum != 0) ? CSkillManager::instance().Get(dwSkillVnum) : NULL;
			if (pSkill)
				len = snprintf(szItemName, sizeof(szItemName), "%s", pSkill->szName);

			break;
		}
		}
		len += snprintf(szItemName + len, sizeof(szItemName) - len, (len > 0) ? " %s" : "%s", GetProto()->szLocaleName);
	}

#ifdef ENABLE_WEAPON_EVOLUTION_SYSTEM
	if (m_pProto)
	{
		if (m_dwEvolution >= 1 && m_dwEvolution <= 5)
		{
			std::string evolutionMergeText = "";
			char evolutionText[5][10] = { "Yaygýn ", "Seyrek ", "Nadir ", "Efsane ", "Eþsiz " };
			evolutionMergeText += evolutionText[(m_dwEvolution - 1)];
			evolutionMergeText += m_pProto->szLocaleName;
			return  evolutionMergeText;
		}
	}
#endif

	std::string returnSzItemName = szItemName;

	return returnSzItemName;
}
#endif

#ifdef ENABLE_SPECIAL_INVENTORY_SYSTEM
bool CItem::IsSkillBook()
{
	switch (GetVnum())
	{
	//	case 90022:
//		case 90023:
		case 39030:
		case 71094:
		case 76034:
		case 71001:
		case 72310:
		case 76026:
		case 39008:
		case 70102:
//		case 91100:
	//	case 91101:

		/*liderlik*/
		case 50301:
		case 50302:
		case 50303:
		
		/*arastirma kitaplari*/
		case 50307:
		case 50308:
		case 50309:
		case 50310:
		
		/*ruh tasi*/
		case 50513:
		
		/*pet kitaplari*/
/* 		case 55010:
		case 55011:
		case 55012:
		case 55013:
		case 55014:
		case 55015:
		case 55016:
		case 55017:
		case 55018:
		case 55019: */
			return true;
	}

	return GetType() == ITEM_SKILLBOOK || GetType() == /*ITEM_SKILLBOOK_P || GetType() == */ITEM_SKILLFORGET;
}

bool CItem::IsUpgradeItem()
{
	switch (GetVnum())
	{
	case 30003:
	case 30004:
	case 30005:
	case 30006:
	case 30007:
	case 30008:
	case 30009:
	case 30010:
	case 30011:
	case 30015:
	case 30016:
	case 30017:
	case 30018:
	case 30019:
	case 30020:
	case 30021:
	case 30022:
	case 30023:
	case 30025:
	case 30027:
	case 30028:
	case 30034:
	case 30035:
	case 30037:
	case 30038:
	case 30039:
	case 30040:
	case 30041:
	case 30042:
	case 30045:
	case 30046:
	case 30047:
	case 30048:
	case 30049:
	case 30050:
	case 30051:
	case 30052:
	case 30053:
	case 30055:
	case 30056:
	case 30057:
	case 30058:
	case 30059:
	case 30060:
	case 30061:
	case 30067:
	case 30069:
	case 30070:
	case 30071:
	case 30072:
	case 30073:
	case 30074:
	case 30075:
	case 30076:
	case 30077:
	case 30078:
	case 30079:
	case 30080:
	case 30081:
	case 30082:
	case 30083:
	case 30084:
	case 30085:
	case 30086:
	case 30087:
	case 30088:
	case 30089:
	case 30090:
	case 30091:
	case 30092:
	case 30192:
	case 30193:
	case 30194:
	case 30195:
	case 30196:
	case 30197:
	case 30198:
	case 30199:
//	case 30344:
	case 30500:
	case 30501:
	case 30502:
	case 30503:
	case 30504:
	case 30505:
	case 30506:
	case 30507:
	case 30508:
	case 30509:
	case 30510:
	case 30511:
	case 30512:
	case 30513:
	case 30514:
	case 30515:
	case 30516:
	case 30517:
	case 30518:
	case 30519:
	case 30520:
	case 30521:
	case 30522:
	case 30523:
	case 30524:
	case 30525:

/* 	case 30600:
	case 30601:
	case 30602:
	case 30603:
	case 30604:
	case 30605:
	case 30606:
	case 30607:
	case 30608:
	case 30609:
	case 30610:
	case 30611:
	case 30612:
	case 30614:
	case 30615:
	case 30616:
	case 30617:
	case 30618:
	case 30619: */

	case 27799:
	case 27992:
	case 27993:
	case 27994:
	case 27987:

	case 81104:
	case 81105:
	case 81106:


/* 	case 33031:
	case 33030:
	case 33029:
	case 30630:
	case 30629:
	case 30628:
	case 30627:
	case 30626:
	case 30625:
	case 30624:
	case 30623:
	case 70097:
	case 70096:
	case 70095:
	case 70094:
	case 70093:
	case 70092:
	case 70091:
	case 70090:
	case 70089:
	case 70088:
	case 70087:
	case 70086:
	case 85000: */

	case 70254:
	case 70253:
	case 70252:
	case 70251:
	case 51001:
	case 50160:
	case 50161:
	case 50162:
	case 50163:
	case 50164:
	case 50165:
	case 50166:
	case 50167:
	case 50168:
	case 50169:
	case 50170:
	case 50171:
	case 50172:
	case 50173:
	case 50174:
	case 50175:
	case 50176:
	case 50177:
	case 50178:
	case 50179:
	case 30622:
	case 30621:
	case 30620:
	case 30252:
	case 30251:
	case 30228:
	case 30227:
	case 30226:
	case 30225:
	case 30224:
	case 30223:
	case 30222:
	case 30221:
	case 30220:
	case 30168:
	case 30167:
	case 30166:
	case 30165:
	case 27990:
	case 71123:
	case 71129:

/* 	case 39072:
	case 39077:
	case 39073:
	case 39078:
	case 39074:
	case 39079:
	case 39075:
	case 39080:
	case 39076: */

	case 40001:
/* 	case 90033:
	case 90034:
	case 90035:
	case 90036:
	case 90037:
	case 90038:
	case 31006:
	case 31066: */

	case 81103:
/* 	case 30542:
	case 30543:
	case 30544:
	case 30545:
	case 30546:
	case 30547:
	case 30548:
	case 30549: */
	
	case 30550:
/* 	case 30551:
	case 30552:
	case 30553:
	case 30554:
	case 30555:
	case 30556:
	case 30557:
	case 30558:
	case 30559:
	case 30560:
	case 30561:
	case 30562:
	case 30563:
	case 30564:
	case 30565:
	case 30566:
	case 30567:
	case 30568:
	case 30569:
	case 30570:
	case 30571:
	case 30572:
	case 30573:
	case 30574:
	case 30575:
	case 30576:
	case 30577:
	case 30578:
	case 30579:
	case 30580:
	case 30581:
	case 30582:
	case 30583:
	case 30584:
	case 30585:
	case 30586:
	case 31062:
	case 31026:
	case 31043:
	case 31057: */
	case 30181:
	case 30182:
	case 30183:
	case 31090:
	case 31005:
	case 30184:
	case 30185:
	case 30186:
	case 30187:
	case 30188:
	case 71022:
	case 71023:
/*	case 31196:
	case 31197:
	case 31198:
	case 31204:
	case 31205:
	case 31206:
	case 31209:
	case 31210:
	case 31211: */
	case 30000:
	case 30001:
	case 30002:
	case 50604:
	case 50605:
	case 50606:
	case 50607:
	case 50608:
	case 50610:
	case 50611:
	case 50612:
	case 50613:
	case 50614:
	case 50615:
	case 50616:
	case 50617:
	case 50618:
	case 50619:
		return true;
	}

	return false;
}

bool CItem::IsStone()
{
	return GetType() == ITEM_METIN;
}

bool CItem::IsGiftBox()
{
	switch (GetVnum())
	{
		case 50187:
		case 50188:
		case 50189:
		case 50190:
		case 50191:
		case 50192:
		case 50193:
		case 50194:
		case 50195:
		case 50196:
			return false;
		
		case 53511:
		case 53512:
		case 53513:
		case 53514:
		case 53515:
		case 53516:
		case 53517:
		case 53518:
		case 53506:
		case 53510:
			return true;
	}
	return GetType() == ITEM_GIFTBOX;
}

bool CItem::IsChanger()
{
	return GetType() == ITEM_USE && 
	(GetSubType() == USE_CHANGE_ATTRIBUTE || GetSubType() == USE_ADD_ATTRIBUTE || GetSubType() == USE_ADD_ATTRIBUTE2 ||
		GetSubType() == USE_CHANGE_COSTUME_ATTR || GetSubType() == USE_RESET_COSTUME_ATTR/* || GetSubType() == USE_CHANGE_PET_ATTRIBUTE*/);
}
#endif

#ifdef ENABLE_BASIC_ITEM_SYSTEM
void CItem::SetBasic(bool b)
{
	is_basic = b;
	UpdatePacket();
}
#endif

#ifdef ENABLE_EXTENDED_PET_SYSTEM
bool CItem::IsPetItem()
{
	if (GetType() == ITEM_UNIQUE && GetSubType() == USE_PET)
		return true;

	return false;
}
#endif

#ifdef ENABLE_SWITCHBOT
bool CItem::CanSwitchItem()
{
	//insiyatifin oldugu kostumler
	if (GetVnum() >= 41005 && GetVnum() <= 41010 || GetVnum() >= 40136 && GetVnum() <= 40153)
		return false;

	//baþlangýç saç stilleri
	if (GetVnum() == 45274 || GetVnum() == 45276)
		return false;
	
	//savasci sac modelleri / dukkan sac modelleri
	if (GetVnum() >= 73001 && GetVnum() <= 73012 || GetVnum() >= 75001 && GetVnum() <= 75012)
		return false;

	//ninja sac modelleri / dukkan sac modelleri
	if (GetVnum() >= 73251 && GetVnum() <= 73262 || GetVnum() >= 75201 && GetVnum() <= 75212)
		return false;

	//sura sac modelleri / dukkan sac modelleri
	if (GetVnum() >= 73501 && GetVnum() <= 73512 || GetVnum() >= 75401 && GetVnum() <= 75412)
		return false;

	//saman sac modelleri / dukkan sac modelleri
	if (GetVnum() >= 73751 && GetVnum() <= 73762 || GetVnum() >= 75601 && GetVnum() <= 75612)
		return false;
	
	//gelinlik ve takim elbise
	if (GetVnum() >= 11901 && GetVnum() <= 11904 || GetVnum() >= 11911 && GetVnum() <= 11914)
		return false;
	
	//buket demeti
	if (GetVnum() == 50201 || GetVnum() == 50202)
		return false;
	
	return true;
}
#endif



