// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_bookmarks/synced_bookmark_tracker.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "base/base64.h"
#include "base/hash/sha1.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/sync/base/client_tag_hash.h"
#include "components/sync/base/time.h"
#include "components/sync/base/unique_position.h"
#include "components/sync/model/entity_data.h"
#include "components/sync/protocol/proto_memory_estimations.h"
#include "ui/base/models/tree_node_iterator.h"

namespace sync_bookmarks {

const base::Feature kInvalidateBookmarkSyncMetadataIfMismatchingGuid{
    "InvalidateBookmarkSyncMetadataIfMismatchingGuid",
    base::FEATURE_DISABLED_BY_DEFAULT};

namespace {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class MetadataClientTagHashStateForUma {
  kAtLeastOneMismatchFound = 0,
  kAllPresentMatchButSomeMissing = 1,
  kAllPresentAndMatching = 2,
  kMaxValue = kAllPresentAndMatching
};

MetadataClientTagHashStateForUma GetMetadataClientTagHashHistogramBucket(
    bool client_tag_mismatch_found,
    bool bookmark_without_client_tag_found) {
  if (client_tag_mismatch_found) {
    return MetadataClientTagHashStateForUma::kAtLeastOneMismatchFound;
  }
  if (bookmark_without_client_tag_found) {
    return MetadataClientTagHashStateForUma::kAllPresentMatchButSomeMissing;
  }
  return MetadataClientTagHashStateForUma::kAllPresentAndMatching;
}

void HashSpecifics(const sync_pb::EntitySpecifics& specifics,
                   std::string* hash) {
  DCHECK_GT(specifics.ByteSize(), 0);
  base::Base64Encode(base::SHA1HashString(specifics.SerializeAsString()), hash);
}

// Returns a map from id to node for all nodes in |model|.
std::unordered_map<int64_t, const bookmarks::BookmarkNode*>
BuildIdToBookmarkNodeMap(const bookmarks::BookmarkModel* model) {
  std::unordered_map<int64_t, const bookmarks::BookmarkNode*>
      id_to_bookmark_node_map;

  // The TreeNodeIterator used below doesn't include the node itself, and hence
  // add the root node separately.
  id_to_bookmark_node_map[model->root_node()->id()] = model->root_node();

  ui::TreeNodeIterator<const bookmarks::BookmarkNode> iterator(
      model->root_node());
  while (iterator.has_next()) {
    const bookmarks::BookmarkNode* node = iterator.Next();
    id_to_bookmark_node_map[node->id()] = node;
  }
  return id_to_bookmark_node_map;
}

}  // namespace

SyncedBookmarkTracker::Entity::Entity(
    const bookmarks::BookmarkNode* bookmark_node,
    std::unique_ptr<sync_pb::EntityMetadata> metadata)
    : bookmark_node_(bookmark_node), metadata_(std::move(metadata)) {
  // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
  // Should be removed after figuring out the reason for the crash.
  CHECK(metadata_);
  if (bookmark_node) {
    DCHECK(!metadata_->is_deleted());
  } else {
    DCHECK(metadata_->is_deleted());
  }
}

SyncedBookmarkTracker::Entity::~Entity() = default;

bool SyncedBookmarkTracker::Entity::IsUnsynced() const {
  return metadata_->sequence_number() > metadata_->acked_sequence_number();
}

bool SyncedBookmarkTracker::Entity::MatchesDataIgnoringParent(
    const syncer::EntityData& data) const {
  if (metadata_->is_deleted() || data.is_deleted()) {
    // In case of deletion, no need to check the specifics.
    return metadata_->is_deleted() == data.is_deleted();
  }
  if (!syncer::UniquePosition::FromProto(metadata_->unique_position())
           .Equals(syncer::UniquePosition::FromProto(data.unique_position))) {
    return false;
  }
  return MatchesSpecificsHash(data.specifics);
}

bool SyncedBookmarkTracker::Entity::MatchesSpecificsHash(
    const sync_pb::EntitySpecifics& specifics) const {
  DCHECK(!metadata_->is_deleted());
  DCHECK_GT(specifics.ByteSize(), 0);
  std::string hash;
  HashSpecifics(specifics, &hash);
  return hash == metadata_->specifics_hash();
}

bool SyncedBookmarkTracker::Entity::has_final_guid() const {
  return metadata_->has_client_tag_hash();
}

bool SyncedBookmarkTracker::Entity::final_guid_matches(
    const std::string& guid) const {
  return metadata_->has_client_tag_hash() &&
         metadata_->client_tag_hash() ==
             syncer::ClientTagHash::FromUnhashed(syncer::BOOKMARKS, guid)
                 .value();
}

void SyncedBookmarkTracker::Entity::set_final_guid(const std::string& guid) {
  metadata_->set_client_tag_hash(
      syncer::ClientTagHash::FromUnhashed(syncer::BOOKMARKS, guid).value());
}

size_t SyncedBookmarkTracker::Entity::EstimateMemoryUsage() const {
  using base::trace_event::EstimateMemoryUsage;
  size_t memory_usage = 0;
  // Include the size of the pointer to the bookmark node.
  memory_usage += sizeof(bookmark_node_);
  memory_usage += EstimateMemoryUsage(metadata_);
  return memory_usage;
}

// static
std::unique_ptr<SyncedBookmarkTracker> SyncedBookmarkTracker::CreateEmpty(
    sync_pb::ModelTypeState model_type_state) {
  // base::WrapUnique() used because the constructor is private.
  auto tracker =
      base::WrapUnique(new SyncedBookmarkTracker(std::move(model_type_state)));
  return tracker;
}

// static
std::unique_ptr<SyncedBookmarkTracker>
SyncedBookmarkTracker::CreateFromBookmarkModelAndMetadata(
    const bookmarks::BookmarkModel* model,
    sync_pb::BookmarkModelMetadata model_metadata) {
  DCHECK(model);

  if (!model_metadata.model_type_state().initial_sync_done()) {
    return nullptr;
  }

  auto tracker =
      CreateEmpty(std::move(*model_metadata.mutable_model_type_state()));

  const CorruptionReason corruption_reason =
      tracker->InitEntitiesFromModelAndMetadata(model,
                                                std::move(model_metadata));

  UMA_HISTOGRAM_ENUMERATION("Sync.BookmarksModelMetadataCorruptionReason",
                            corruption_reason);

  if (corruption_reason != CorruptionReason::NO_CORRUPTION) {
    return nullptr;
  }

  return tracker;
}

SyncedBookmarkTracker::~SyncedBookmarkTracker() = default;

const SyncedBookmarkTracker::Entity* SyncedBookmarkTracker::GetEntityForSyncId(
    const std::string& sync_id) const {
  auto it = sync_id_to_entities_map_.find(sync_id);
  return it != sync_id_to_entities_map_.end() ? it->second.get() : nullptr;
}

SyncedBookmarkTracker::Entity* SyncedBookmarkTracker::GetMutableEntityForSyncId(
    const std::string& sync_id) {
  auto it = sync_id_to_entities_map_.find(sync_id);
  return it != sync_id_to_entities_map_.end() ? it->second.get() : nullptr;
}

const SyncedBookmarkTracker::Entity*
SyncedBookmarkTracker::GetEntityForBookmarkNode(
    const bookmarks::BookmarkNode* node) const {
  auto it = bookmark_node_to_entities_map_.find(node);
  return it != bookmark_node_to_entities_map_.end() ? it->second : nullptr;
}

void SyncedBookmarkTracker::Add(const std::string& sync_id,
                                const bookmarks::BookmarkNode* bookmark_node,
                                int64_t server_version,
                                base::Time creation_time,
                                const sync_pb::UniquePosition& unique_position,
                                const sync_pb::EntitySpecifics& specifics) {
  DCHECK_GT(specifics.ByteSize(), 0);
  auto metadata = std::make_unique<sync_pb::EntityMetadata>();
  metadata->set_is_deleted(false);
  metadata->set_server_id(sync_id);
  metadata->set_server_version(server_version);
  metadata->set_creation_time(syncer::TimeToProtoTime(creation_time));
  metadata->set_modification_time(syncer::TimeToProtoTime(creation_time));
  metadata->set_sequence_number(0);
  metadata->set_acked_sequence_number(0);
  metadata->mutable_unique_position()->CopyFrom(unique_position);
  // For any newly added bookmark, be it a local creation or a remote one, the
  // authoritative final GUID is known from start.
  metadata->set_client_tag_hash(syncer::ClientTagHash::FromUnhashed(
                                    syncer::BOOKMARKS, bookmark_node->guid())
                                    .value());
  HashSpecifics(specifics, metadata->mutable_specifics_hash());
  auto entity = std::make_unique<Entity>(bookmark_node, std::move(metadata));
  bookmark_node_to_entities_map_[bookmark_node] = entity.get();
  // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
  // Should be removed after figuring out the reason for the crash.
  CHECK_EQ(0U, sync_id_to_entities_map_.count(sync_id));
  sync_id_to_entities_map_[sync_id] = std::move(entity);
}

void SyncedBookmarkTracker::Update(
    const std::string& sync_id,
    int64_t server_version,
    base::Time modification_time,
    const sync_pb::UniquePosition& unique_position,
    const sync_pb::EntitySpecifics& specifics) {
  DCHECK_GT(specifics.ByteSize(), 0);
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  DCHECK_EQ(entity->metadata()->server_id(), sync_id);

  entity->metadata()->set_server_version(server_version);
  entity->metadata()->set_modification_time(
      syncer::TimeToProtoTime(modification_time));
  *entity->metadata()->mutable_unique_position() = unique_position;
  HashSpecifics(specifics, entity->metadata()->mutable_specifics_hash());
  // TODO(crbug.com/516866): in case of conflict, the entity might exist in
  // |ordered_local_tombstones_| as well if it has been locally deleted.
}

void SyncedBookmarkTracker::UpdateServerVersion(const std::string& sync_id,
                                                int64_t server_version) {
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  entity->metadata()->set_server_version(server_version);
}

void SyncedBookmarkTracker::PopulateFinalGuid(const std::string& sync_id,
                                              const std::string& guid) {
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  entity->set_final_guid(guid);
}

void SyncedBookmarkTracker::MarkCommitMayHaveStarted(
    const std::string& sync_id) {
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  entity->set_commit_may_have_started(true);
}

void SyncedBookmarkTracker::MarkDeleted(const std::string& sync_id) {
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  entity->metadata()->set_is_deleted(true);
  // Clear all references to the deleted bookmark node.
  bookmark_node_to_entities_map_.erase(entity->bookmark_node());
  entity->clear_bookmark_node();
  // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
  // Should be removed after figuring out the reason for the crash.
  CHECK_EQ(0, std::count(ordered_local_tombstones_.begin(),
                         ordered_local_tombstones_.end(), entity));
  ordered_local_tombstones_.push_back(entity);
}

void SyncedBookmarkTracker::Remove(const std::string& sync_id) {
  const Entity* entity = GetEntityForSyncId(sync_id);
  DCHECK(entity);
  bookmark_node_to_entities_map_.erase(entity->bookmark_node());
  base::Erase(ordered_local_tombstones_, entity);
  sync_id_to_entities_map_.erase(sync_id);
}

void SyncedBookmarkTracker::IncrementSequenceNumber(
    const std::string& sync_id) {
  // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
  // Should be switched to a DCHECK after figuring out the reason for the crash.
  CHECK_NE(0U, sync_id_to_entities_map_.count(sync_id));
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  // TODO(crbug.com/516866): Update base hash specifics here if the entity is
  // not already out of sync.
  entity->metadata()->set_sequence_number(
      entity->metadata()->sequence_number() + 1);
}

sync_pb::BookmarkModelMetadata
SyncedBookmarkTracker::BuildBookmarkModelMetadata() const {
  sync_pb::BookmarkModelMetadata model_metadata;
  for (const std::pair<const std::string, std::unique_ptr<Entity>>& pair :
       sync_id_to_entities_map_) {
    DCHECK(pair.second) << " for ID " << pair.first;
    DCHECK(pair.second->metadata()) << " for ID " << pair.first;
    if (pair.second->metadata()->is_deleted()) {
      // Deletions will be added later because they need to maintain the same
      // order as in |ordered_local_tombstones_|.
      continue;
    }
    DCHECK(pair.second->bookmark_node());
    sync_pb::BookmarkMetadata* bookmark_metadata =
        model_metadata.add_bookmarks_metadata();
    bookmark_metadata->set_id(pair.second->bookmark_node()->id());
    *bookmark_metadata->mutable_metadata() = *pair.second->metadata();
  }
  // Add pending deletions.
  for (const Entity* tombstone_entity : ordered_local_tombstones_) {
    DCHECK(tombstone_entity);
    DCHECK(tombstone_entity->metadata());
    DCHECK(tombstone_entity->metadata()->is_deleted());
    sync_pb::BookmarkMetadata* bookmark_metadata =
        model_metadata.add_bookmarks_metadata();
    *bookmark_metadata->mutable_metadata() = *tombstone_entity->metadata();
  }
  *model_metadata.mutable_model_type_state() = model_type_state_;
  return model_metadata;
}

bool SyncedBookmarkTracker::HasLocalChanges() const {
  for (const std::pair<const std::string, std::unique_ptr<Entity>>& pair :
       sync_id_to_entities_map_) {
    Entity* entity = pair.second.get();
    if (entity->IsUnsynced()) {
      return true;
    }
  }
  return false;
}

std::vector<const SyncedBookmarkTracker::Entity*>
SyncedBookmarkTracker::GetAllEntities() const {
  std::vector<const SyncedBookmarkTracker::Entity*> entities;
  for (const std::pair<const std::string, std::unique_ptr<Entity>>& pair :
       sync_id_to_entities_map_) {
    entities.push_back(pair.second.get());
  }
  return entities;
}

std::vector<const SyncedBookmarkTracker::Entity*>
SyncedBookmarkTracker::GetEntitiesWithLocalChanges(size_t max_entries) const {
  std::vector<const SyncedBookmarkTracker::Entity*> entities_with_local_changes;
  // Entities with local non deletions should be sorted such that parent
  // creation/update comes before child creation/update.
  for (const std::pair<const std::string, std::unique_ptr<Entity>>& pair :
       sync_id_to_entities_map_) {
    Entity* entity = pair.second.get();
    if (entity->metadata()->is_deleted()) {
      // Deletions are stored sorted in |ordered_local_tombstones_| and will be
      // added later.
      continue;
    }
    if (entity->IsUnsynced()) {
      entities_with_local_changes.push_back(entity);
    }
  }
  std::vector<const SyncedBookmarkTracker::Entity*> ordered_local_changes =
      ReorderUnsyncedEntitiesExceptDeletions(entities_with_local_changes);
  for (const Entity* tombstone_entity : ordered_local_tombstones_) {
    // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
    // Should be removed after figuring out the reason for the crash.
    CHECK_EQ(0, std::count(ordered_local_changes.begin(),
                           ordered_local_changes.end(), tombstone_entity));
    ordered_local_changes.push_back(tombstone_entity);
  }
  if (ordered_local_changes.size() > max_entries) {
    // TODO(crbug.com/516866): Should be smart and stop building the vector
    // when |max_entries| is reached.
    return std::vector<const SyncedBookmarkTracker::Entity*>(
        ordered_local_changes.begin(),
        ordered_local_changes.begin() + max_entries);
  }
  return ordered_local_changes;
}

SyncedBookmarkTracker::SyncedBookmarkTracker(
    sync_pb::ModelTypeState model_type_state)
    : model_type_state_(std::move(model_type_state)) {}

SyncedBookmarkTracker::CorruptionReason
SyncedBookmarkTracker::InitEntitiesFromModelAndMetadata(
    const bookmarks::BookmarkModel* model,
    sync_pb::BookmarkModelMetadata model_metadata) {
  DCHECK(model_type_state_.initial_sync_done());

  // Used for histograms.
  bool client_tag_mismatch_found = false;
  bool bookmark_without_client_tag_found = false;

  // Build a temporary map to look up bookmark nodes efficiently by node ID.
  std::unordered_map<int64_t, const bookmarks::BookmarkNode*>
      id_to_bookmark_node_map = BuildIdToBookmarkNodeMap(model);

  // Collect ids of non-deletion entries in the metadata.
  std::vector<int> metadata_node_ids;

  for (sync_pb::BookmarkMetadata& bookmark_metadata :
       *model_metadata.mutable_bookmarks_metadata()) {
    if (!bookmark_metadata.metadata().has_server_id()) {
      DLOG(ERROR) << "Error when decoding sync metadata: Entities must contain "
                     "server id.";
      return CorruptionReason::MISSING_SERVER_ID;
    }

    const std::string sync_id = bookmark_metadata.metadata().server_id();
    if (sync_id_to_entities_map_.count(sync_id) != 0) {
      DLOG(ERROR) << "Error when decoding sync metadata: Duplicated server id.";
      return CorruptionReason::DUPLICATED_SERVER_ID;
    }

    // Handle tombstones.
    if (bookmark_metadata.metadata().is_deleted()) {
      if (bookmark_metadata.has_id()) {
        DLOG(ERROR) << "Error when decoding sync metadata: Tombstones "
                       "shouldn't have a bookmark id.";
        return CorruptionReason::BOOKMARK_ID_IN_TOMBSTONE;
      }

      auto tombstone_entity = std::make_unique<Entity>(
          /*node=*/nullptr, std::make_unique<sync_pb::EntityMetadata>(std::move(
                                *bookmark_metadata.mutable_metadata())));
      ordered_local_tombstones_.push_back(tombstone_entity.get());
      sync_id_to_entities_map_[sync_id] = std::move(tombstone_entity);
      continue;
    }

    // Non-tombstones.
    DCHECK(!bookmark_metadata.metadata().is_deleted());

    if (!bookmark_metadata.has_id()) {
      DLOG(ERROR)
          << "Error when decoding sync metadata: Bookmark id is missing.";
      return CorruptionReason::MISSING_BOOKMARK_ID;
    }

    const bookmarks::BookmarkNode* node =
        id_to_bookmark_node_map[bookmark_metadata.id()];

    if (!node) {
      DLOG(ERROR) << "Error when decoding sync metadata: unknown Bookmark id.";
      return CorruptionReason::UNKNOWN_BOOKMARK_ID;
    }

    if (!node->is_permanent_node() &&
        !bookmark_metadata.metadata().has_client_tag_hash()) {
      bookmark_without_client_tag_found = true;
    }

    // The client-tag-hash is optional, but if it does exist, it is expected to
    // be equal to the hash of the bookmark's GUID. This can be hit for example
    // if local bookmark GUIDs were reassigned upon startup due to duplicates
    // (which is a BookmarkModel invariant violation and should be impossible).
    // TODO(crbug.com/1032052): Simplify this code once all local sync metadata
    // is required to populate the client tag (and be considered invalid
    // otherwise).
    if (bookmark_metadata.metadata().has_client_tag_hash() &&
        !node->is_permanent_node() &&
        bookmark_metadata.metadata().client_tag_hash() !=
            syncer::ClientTagHash::FromUnhashed(syncer::BOOKMARKS, node->guid())
                .value()) {
      DLOG(ERROR) << "Bookmark GUID does not match the client tag.";
      client_tag_mismatch_found = true;

      if (base::FeatureList::IsEnabled(
              kInvalidateBookmarkSyncMetadataIfMismatchingGuid)) {
        return CorruptionReason::BOOKMARK_GUID_MISMATCH;
      } else {
        // Simply clear the field, although it's most likely accurate, since it
        // isn't useful while the actual (unhashed) GUID is unknown.
        bookmark_metadata.mutable_metadata()->clear_client_tag_hash();
      }
    }

    auto entity = std::make_unique<Entity>(
        node, std::make_unique<sync_pb::EntityMetadata>(
                  std::move(*bookmark_metadata.mutable_metadata())));
    entity->set_commit_may_have_started(true);
    bookmark_node_to_entities_map_[node] = entity.get();
    sync_id_to_entities_map_[sync_id] = std::move(entity);
  }

  // See if there are untracked entities in the BookmarkModel.
  std::vector<int> model_node_ids;
  ui::TreeNodeIterator<const bookmarks::BookmarkNode> iterator(
      model->root_node());
  while (iterator.has_next()) {
    const bookmarks::BookmarkNode* node = iterator.Next();
    if (!model->client()->CanSyncNode(node)) {
      continue;
    }
    if (bookmark_node_to_entities_map_.count(node) == 0) {
      return CorruptionReason::UNTRACKED_BOOKMARK;
    }
  }

  UMA_HISTOGRAM_ENUMERATION(
      "Sync.BookmarkModelMetadataClientTagState",
      GetMetadataClientTagHashHistogramBucket(
          client_tag_mismatch_found, bookmark_without_client_tag_found));

  CheckAllNodesTracked(model);
  return CorruptionReason::NO_CORRUPTION;
}

std::vector<const SyncedBookmarkTracker::Entity*>
SyncedBookmarkTracker::ReorderUnsyncedEntitiesExceptDeletions(
    const std::vector<const SyncedBookmarkTracker::Entity*>& entities) const {
  // This method sorts the entities with local non deletions such that parent
  // creation/update comes before child creation/update.

  // The algorithm works by constructing a forest of all non-deletion updates
  // and then traverses each tree in the forest recursively:
  // 1. Iterate over all entities and collect all nodes in |nodes|.
  // 2. Iterate over all entities again and node that is a child of another
  //    node. What's left in |nodes| are the roots of the forest.
  // 3. Start at each root in |nodes|, emit the update and recurse over its
  //    children.
  std::set<const bookmarks::BookmarkNode*> nodes;
  // Collect nodes with updates
  for (const SyncedBookmarkTracker::Entity* entity : entities) {
    DCHECK(entity->IsUnsynced());
    DCHECK(!entity->metadata()->is_deleted());
    DCHECK(entity->bookmark_node());
    nodes.insert(entity->bookmark_node());
  }
  // Remove those who are direct children of another node.
  for (const SyncedBookmarkTracker::Entity* entity : entities) {
    const bookmarks::BookmarkNode* node = entity->bookmark_node();
    for (const auto& child : node->children())
      nodes.erase(child.get());
  }
  // |nodes| contains only roots of all trees in the forest all of which are
  // ready to be processed because their parents have no pending updates.
  std::vector<const SyncedBookmarkTracker::Entity*> ordered_entities;
  for (const bookmarks::BookmarkNode* node : nodes) {
    TraverseAndAppend(node, &ordered_entities);
  }
  return ordered_entities;
}

void SyncedBookmarkTracker::TraverseAndAppend(
    const bookmarks::BookmarkNode* node,
    std::vector<const SyncedBookmarkTracker::Entity*>* ordered_entities) const {
  const SyncedBookmarkTracker::Entity* entity = GetEntityForBookmarkNode(node);
  DCHECK(entity);
  DCHECK(entity->IsUnsynced());
  DCHECK(!entity->metadata()->is_deleted());
  ordered_entities->push_back(entity);
  // Recurse for all children.
  for (const auto& child : node->children()) {
    const SyncedBookmarkTracker::Entity* child_entity =
        GetEntityForBookmarkNode(child.get());
    DCHECK(child_entity);
    if (!child_entity->IsUnsynced()) {
      // If the entity has no local change, no need to check its children. If
      // any of the children would have a pending commit, it would be a root for
      // a separate tree in the forest built in
      // ReorderEntitiesWithLocalNonDeletions() and will be handled by another
      // call to TraverseAndAppend().
      continue;
    }
    if (child_entity->metadata()->is_deleted()) {
      // Deletion are stored sorted in |ordered_local_tombstones_| and will be
      // added later.
      continue;
    }
    TraverseAndAppend(child.get(), ordered_entities);
  }
}

void SyncedBookmarkTracker::UpdateUponCommitResponse(
    const std::string& old_id,
    const std::string& new_id,
    int64_t acked_sequence_number,
    int64_t server_version) {
  // TODO(crbug.com/516866): Update specifics if we decide to keep it.
  Entity* entity = GetMutableEntityForSyncId(old_id);
  if (!entity) {
    DLOG(WARNING) << "Trying to update a non existing entity.";
    return;
  }

  entity->metadata()->set_acked_sequence_number(acked_sequence_number);
  entity->metadata()->set_server_version(server_version);
  // If there are no pending commits, remove tombstones.
  if (!entity->IsUnsynced() && entity->metadata()->is_deleted()) {
    Remove(old_id);
    return;
  }

  UpdateSyncForLocalCreationIfNeeded(old_id, new_id);
}

void SyncedBookmarkTracker::UpdateSyncForLocalCreationIfNeeded(
    const std::string& old_id,
    const std::string& new_id) {
  if (old_id == new_id) {
    return;
  }
  // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
  // Should be removed after figuring out the reason for the crash.
  CHECK_EQ(1U, sync_id_to_entities_map_.count(old_id));
  CHECK_EQ(0U, sync_id_to_entities_map_.count(new_id));

  std::unique_ptr<Entity> entity =
      std::move(sync_id_to_entities_map_.at(old_id));
  entity->metadata()->set_server_id(new_id);
  sync_id_to_entities_map_[new_id] = std::move(entity);
  sync_id_to_entities_map_.erase(old_id);
}

void SyncedBookmarkTracker::UpdateBookmarkNodePointer(
    const bookmarks::BookmarkNode* old_node,
    const bookmarks::BookmarkNode* new_node) {
  if (old_node == new_node) {
    return;
  }
  bookmark_node_to_entities_map_[new_node] =
      bookmark_node_to_entities_map_[old_node];
  bookmark_node_to_entities_map_[new_node]->set_bookmark_node(new_node);
  bookmark_node_to_entities_map_.erase(old_node);
}

void SyncedBookmarkTracker::AckSequenceNumber(const std::string& sync_id) {
  Entity* entity = GetMutableEntityForSyncId(sync_id);
  DCHECK(entity);
  entity->metadata()->set_acked_sequence_number(
      entity->metadata()->sequence_number());
}

bool SyncedBookmarkTracker::IsEmpty() const {
  return sync_id_to_entities_map_.empty();
}

size_t SyncedBookmarkTracker::EstimateMemoryUsage() const {
  using base::trace_event::EstimateMemoryUsage;
  size_t memory_usage = 0;
  memory_usage += EstimateMemoryUsage(sync_id_to_entities_map_);
  memory_usage += EstimateMemoryUsage(bookmark_node_to_entities_map_);
  memory_usage += EstimateMemoryUsage(ordered_local_tombstones_);
  memory_usage += EstimateMemoryUsage(model_type_state_);
  return memory_usage;
}

size_t SyncedBookmarkTracker::TrackedEntitiesCountForTest() const {
  return sync_id_to_entities_map_.size();
}

size_t SyncedBookmarkTracker::TrackedBookmarksCountForDebugging() const {
  return bookmark_node_to_entities_map_.size();
}

size_t SyncedBookmarkTracker::TrackedUncommittedTombstonesCountForDebugging()
    const {
  return ordered_local_tombstones_.size();
}

void SyncedBookmarkTracker::CheckAllNodesTracked(
    const bookmarks::BookmarkModel* bookmark_model) const {
  // TODO(crbug.com/516866): The method is added to debug some crashes.
  // Since it's relatively expensive, it should run on debug enabled
  // builds only after the root cause is found.
  CHECK(GetEntityForBookmarkNode(bookmark_model->bookmark_bar_node()));
  CHECK(GetEntityForBookmarkNode(bookmark_model->other_node()));
  CHECK(GetEntityForBookmarkNode(bookmark_model->mobile_node()));

  ui::TreeNodeIterator<const bookmarks::BookmarkNode> iterator(
      bookmark_model->root_node());
  while (iterator.has_next()) {
    const bookmarks::BookmarkNode* node = iterator.Next();
    if (!bookmark_model->client()->CanSyncNode(node)) {
      // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
      // Should be converted to a DCHECK after the root cause if found.
      CHECK(!GetEntityForBookmarkNode(node));
      continue;
    }
    // Root node is usually tracked, unless the sync data has been provided by
    // the USS migrator.
    if (node == bookmark_model->root_node()) {
      continue;
    }
    // TODO(crbug.com/516866): The below CHECK is added to debug some crashes.
    // Should be converted to a DCHECK after the root cause if found.
    CHECK(GetEntityForBookmarkNode(node));
  }
}

}  // namespace sync_bookmarks
