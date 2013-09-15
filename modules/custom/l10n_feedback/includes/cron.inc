<?php
/**
 * @file
 * Functions called by cron.
 */


/*
 * Implements hook_cron_queue_info().
 */
function l10n_feedback_cron_queue_info() {

  $queues['notifications'] = array(
    'worker callback' => 'l10n_feedback_send_notification_by_email',
    'time' => 240,
  );
  $queues['export_project'] = array(
    'worker callback' => 'l10n_feedback_export_project',
    'time' => 2000,
  );
  $queues['l10n_feedback_delete_fake_users'] = array(
    'worker callback' => 'l10n_feedback_delete_fake_user',
    'time' => 30,
  );
  return $queues;
}

/*
 * Implements hook_cron().
 */
function l10n_feedback_cron() {
  l10n_feedback_cron_nightly();
}

/**
 * Make sure that cron functions runs only once per night,
 * no matter how often cron is called.
 */
function l10n_feedback_cron_nightly() {
  // check whether the crontime has been reached yet
  $crontime = variable_get('btr_crontime', '0');
  if (time() < $crontime)  return;

  // set the crontime after the next midnight (after 03:00 AM)
  $crontime = strtotime(date('Y-m-d 02:55')) + 24*60*60;
  variable_set('btr_crontime', $crontime);

  // Call cron functions.
  l10n_feedback_delete_old_export_files();
  l10n_feedback_send_strings_for_review();
  l10n_feedback_cleanup_fake_users();
}

/**
 * Delete export files that are older than 2 days
 */
function l10n_feedback_delete_old_export_files() {
  $export_path = variable_get('btr_export_path', '/var/www/exports');
  exec("find $export_path/* -mtime +2 -delete");
}

/**
 * Send by email a string for review to all the active users.
 */
function l10n_feedback_send_strings_for_review() {

  // return true if we should NOT send a string by email to the given account
  function _l10n_feedback_dont_send_email($account) {
    // skip admin, disabled accounts, and users that have never logged in
    if ($account->uid < 2 or $account->status != 1 or $account->login == 0) {
      return TRUE;
    }

    // skip users that have unchecked email on feedback_channels
    $field_arr = $account->field_feedback_channels['und'];
    $feedback_channels = array();
    if (is_array($field_arr)) {
      foreach ($field_arr as $item) {
        $feedback_channels[] = $item['value'];
      }
    }
    if ( ! in_array('email', $feedback_channels) )  {
      return TRUE;
    }

    // otherwise send email
    return FALSE;
  }

  $queue = DrupalQueue::get('notifications');
  $queue->createQueue();  // There is no harm in trying to recreate existing.

  $accounts = entity_load('user');
  foreach ($accounts as $account) {
    if (_l10n_feedback_dont_send_email($account))  continue;

    // get a sguid according to the user preferencies
    require_once(dirname(__FILE__) . '/translations/vote.inc');
    $sguid = l10n_feedback_get_sguid($account->uid);

    $message_params = array(
      'type' => 'string-to-be-reviewed',
      'uid' => $account->uid,
      'sguid' => $sguid,
      'username' => $account->name,
      'recipient' => $account->name .' <' . $account->mail . '>',
    );
    $queue->createItem((object)$message_params);
  }
}

/**
 * The callback function called from cron_queue export_project.
 */
function l10n_feedback_export_project($export_params) {

  // Make sure that exports do not run in parallel,
  // so that the server is not loaded.
  if (!lock_acquire('export_project', 3000)) {
    // If we cannot get the lock, just stop the execution, do not return,
    // because after the callback function returns, the cron_queue will
    // remove the item from the queue, no matter whether it is processed or not.
    exit();
  }

  // Allow the export script to run until completion.
  set_time_limit(0);

  $origin = $export_params->origin;
  $project = $export_params->project;
  $export_mode = $export_params->export_mode;
  $voters = $export_params->voters;
  $account = user_load($export_params->uid);

  // Get the full path of the export file.
  $lng = variable_get('btr_translation_lng', 'fr');
  $export_dir = variable_get('btr_export_path', '/var/www/exports');
  $username = strtr(strtolower($account->name), ' ', '_');
  $filename = "$username.$origin-$project-$lng";
  $exports_url = url('translations/exports', array('absolute' => TRUE));
  $export_url_tgz = $exports_url . '/' . $filename . '.tgz';
  $export_url_diff = $exports_url . '/' . $filename . '.diff';
  $export_url_ediff = $exports_url . '/' . $filename . '.ediff';
  exec("rm -f $export_dir/$filename.tgz $export_dir/$filename.diff $export_dir/$filename.ediff");

  // Run the export script (with lowered priority).
  $make_diff_sh = drupal_get_path('module', 'l10n_feedback') . '/data/export/make_diff.sh';
  $export_cmd = "nice $make_diff_sh $origin $project $lng $export_dir $filename";
  $export_cmd = "export QUIET=true; $export_cmd";
  if ($export_mode == 'preferred_by_me' or $export_mode == 'preferred_by_my_friends') {
    $export_cmd = "export PO_EXPORT_MODE='preferred' ; export PO_EXPORT_VOTERS='$voters' ; $export_cmd";
  }
  exec($export_cmd);

  // Notify the user that the export is done.
  $queue = DrupalQueue::get('notifications');
  $queue->createQueue();  // There is no harm in trying to recreate existing.
  $params = array(
    'type' => 'notify-that-export-is-done',
    'uid' => $account->uid,
    'project' => $origin . '/' . $project,
    'username' => $account->name,
    'recipient' => $account->name .' <' . $account->mail . '>',
    'export_url_tgz' => $export_url_tgz,
    'export_url_diff' => $export_url_diff,
    'export_url_ediff' => $export_url_ediff,
  );
  $queue->createItem((object)$params);

  // This export is done, allow any other exports to run.
  lock_release('export_project');
}


/**
 * Cleanup the users that were registered a week ago,
 * but have never accessed the site since then
 * (most probably they are created by spamming robots).
 * Reference:
 * http://drupal.stackexchange.com/questions/54006/how-can-i-prevent-users-from-entering-invalid-e-mail-addresses-at-registration?newsletter=1&nlcode=43535%7c8b76
 */
function l10n_feedback_cleanup_fake_users() {
  $query = new EntityFieldQuery();

  $query->entityCondition('entity_type', 'user')
    ->entityCondition('entity_id', 1, '>')
    ->propertyCondition('access', 0)
    ->propertyCondition('created', REQUEST_TIME - 7*24*60*60, '<')
    ->addTag('DANGEROUS_ACCESS_CHECK_OPT_OUT');

  $result = $query->execute();

  if (isset($result['user'])) {
    $queue = DrupalQueue::get('l10n_feedback_delete_fake_users');
    foreach (array_keys($result['user']) as $uid) {
      $queue->createItem($uid);
    }
  }
}

/**
 * This function is a callback from the cron-queue,
 * which is used to delete the user with the given $uid.
 */
function l10n_feedback_delete_fake_user($uid) {
  // Delete from the drupal table 'users'
  user_delete($uid);

  // Delete from the table 'btr_users'
  $lng = variable_get('btr_translation_lng', 'fr');
  btr_user_delete($lng, $uid);
}